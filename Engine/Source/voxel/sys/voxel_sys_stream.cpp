/*************************************************************************************************************
\file       voxel_sys_stream.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      voxel::System, the streamed ("endless") terrain half. The rest of the class is in voxel_sys.cpp;
            this file is everything that only happens when the world is a Storage::Sparse one fed by
            stream::CellStreamer.

            The division of labour: the streamer decides what should be resident and builds cells on its
            own threads, never touching the World. This file is the only place pages are installed and
            removed, always inside the write window update() opens (no mesh job pending), in this order:

                queued edits -> evictions -> installs -> re-mip + remesh the edits
                             -> mesh the new cells -> collision near the focus

            so a mesh job never sees a page appear or vanish, exactly as it never sees an edit land.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "voxel_sys.hpp"

// Voxel
#include "../events/voxel_events.hpp"
#include "../render/voxel_renderer.hpp"
#include "../stream/cell_streamer.hpp"
#include "../stream/edit_store.hpp"
#include "../world/voxel_math.hpp"

// Physics
#include "../../physics/sys/phy_sys.hpp"

// Components
#include "../../components/component_types/voxel_world.hpp"

// Profiling
#include "../../profile/profiler.hpp"

// stds
#include <algorithm>
#include <iostream>


namespace voxel
{
	namespace
	{
		/*!
			@brief
				Packs a cell coordinate into one 64-bit key (z high, x low).
			@param cell
				The cell
			@return
				Its key
			*/
		std::uint64_t cellKey(const glm::ivec2& cell)
		{
			return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cell.y)) << 32) | static_cast<std::uint32_t>(cell.x);
		}

		/*!
			@brief
				Unpacks a key made by cellKey().
			@param key
				The key
			@return
				The cell coordinate
			*/
		glm::ivec2 cellOfKey(std::uint64_t key)
		{
			return { static_cast<int>(static_cast<std::uint32_t>(key)), static_cast<int>(static_cast<std::uint32_t>(key >> 32)) };
		}

		// How often the memory cap looks at the pool, in frames: long enough
		// for an eviction to show up in the numbers before the next one.
		constexpr int kCapPeriodFrames = 45;

		// The rings are never pulled in further than this.
		constexpr float kMinRadiusScale = 0.4f;
	}

	// =============================== Lifecycle ================================

	/*!
		@brief
			Replaces the world with a streamed one: builds the streamer, installs an
			empty Sparse world, and routes undos of evicted sections to the edit store.

			Main thread. Throws what the recipe or a material gets wrong, before
			anything of the current world is touched.
		@param recipe
			An endless recipe
		@param label
			The name the world gets
		@param generated
			Whether it was generated rather than read from disk (the recipe is not saved yet)
		*/
	void System::startStream(Recipe recipe, std::string label, bool generated)
	{
		VOX_PROFILE_FUNCTION_C(profile::colour::kVoxel);

		const stream::Settings settings = stream::Settings::load(*resources_);

		// Throws before anything of the current world is touched.
		auto streamer = std::make_unique<stream::CellStreamer>(recipe, materials_, settings);
		auto world = std::make_shared<World>(streamer->generator().dims(), World::Storage::Sparse);

		installWorld(std::move(world), label);		// stops the previous stream, if any

		streamer_ = std::move(streamer);
		editStore_ = std::make_unique<stream::EditStore>(settings.editStoreBytes_);
		editedSections_.clear();
		collidableCells_.clear();
		capCooldown_ = 0;
		capLogged_ = false;

		// An undo that reaches a section which has streamed out since goes to
		// the kept edits instead; the cell is built again around it.
		history_.setOffline([this](const glm::ivec3& section, const edit::SectionSnapshot& snapshot)
		{
			const glm::ivec2 cell = World::cellOf(0, section.x, section.z);
			editStore_->put(world_->sectionIndex(0, section.x, section.y, section.z), section, snapshot);

			// Whatever of the cell is resident, or on its way, was built from
			// the edits as they were.
			streamer_->invalidate(cell);
			if (const int held = streamer_->resident(cell); held != stream::CellStreamer::kAbsent)
				streamEvict(cell, held, stream::CellStreamer::kAbsent);
		});

		if (renderer_)
			renderer_->setWorld(world_, settings.arenaBytes_);

		recipe_ = std::move(recipe);
		lastInfo_ = {};
		lastInfo_.seed_ = recipe_->seed_;
		modified_ = generated;		// "the recipe is not on disk yet"; hand edits never set it

		setProgress(true, 0.f, "streaming");
		std::cout << "[voxel] streaming " << label << ": rings " << settings.radius_[0] << " / " << settings.radius_[1] << " / "
			<< settings.radius_[2] << " / " << settings.radius_[3] << " m, tiles of " << streamer_->generator().coreVoxels() << " voxels\n";
	}

	/*!
		@brief
			Stops the stream's workers and forgets its kept edits.
		*/
	void System::stopStream()
	{
		if (!streamer_)
			return;

		history_.setOffline(nullptr);

		// Waits for the cells being built; a tile notices the cancel between passes.
		streamer_.reset();
		editStore_.reset();
		editedSections_.clear();
		collidableCells_.clear();
	}

	// ================================ Per frame ================================

	/*!
		@brief
			Per frame, outside the write window: moves the focus, applies the memory
			cap to the ring radii, and schedules new builds.
		*/
	void System::streamSchedule()
	{
		streamer_->setFocus(streamFocus_);

		// The memory cap. Pulling every ring in by the same share keeps their
		// proportions, and with them the look; the evictions that follow are
		// the ordinary ones.
		if (--capCooldown_ <= 0)
		{
			capCooldown_ = kCapPeriodFrames;

			const std::size_t used = sectionPool().stats().bytesInUse();
			const std::size_t budget = streamer_->settings().poolBudgetBytes_;
			const float scale = streamer_->radiusScale();

			if (used > budget && scale > kMinRadiusScale)
			{
				streamer_->setRadiusScale(std::max(kMinRadiusScale, scale * 0.92f));
				if (!capLogged_)
				{
					capLogged_ = true;
					std::cerr << "[voxel] the section pool is over its " << (budget >> 20) << " MiB budget (" << (used >> 20)
						<< " MiB): pulling the streaming rings in. Raise poolBudgetMiB or lower the radii in Assets/voxel/stream.json\n";
				}
			}
			else if (used < budget / 10 * 7 && scale < 1.f)
			{
				streamer_->setRadiusScale(std::min(1.f, scale * 1.04f));
			}
		}

		streamer_->schedule(*editStore_);
	}

	/*!
		@brief
			Inside the write window: lets cells go, puts finished ones in (within
			the install budget), then collects collision requests.
		@param meshes
			Receives the mesh jobs the new cells need
		*/
	void System::streamApply(std::vector<MeshRequest>& meshes)
	{
		VOX_PROFILE_FUNCTION_C(profile::colour::kVoxel);

		// An open transaction's history is about to capture the sections it
		// touched "as they are now" (History::commit); they have to be there.
		const bool pinned = history_.isOpen() || !editBase_.empty();

		for (const stream::CellStreamer::Eviction& e : streamer_->evictions(pinned))
			streamEvict(e.cell_, e.from_, e.to_);

		// A stroke waiting for this same window gets most of it.
		const stream::Settings& s = streamer_->settings();
		const int budget = editsPending() || history_.isOpen() ? s.installJobsEditing_ : s.installJobs_;

		for (CellSections& cell : streamer_->takeFinished(static_cast<std::size_t>(budget)))
			streamInstall(cell, meshes);

		streamColliders(meshes);
	}

	/*!
		@brief
			Drops a cell's pages from one detail level up to another; at level 0 its
			hand edits go to the edit store and its bodies are removed.
		@param cell
			The cell
		@param from
			The finest level to drop
		@param to
			The detail the cell keeps (levels below it go)
		*/
	void System::streamEvict(const glm::ivec2& cell, int from, int to)
	{
		const int last = std::min(to, kLodLevels);

		for (int level = from; level < last; ++level)
		{
			if (level == 0)
			{
				// What a brush did here cannot be generated again.
				for (auto it = editedSections_.begin(); it != editedSections_.end();)
				{
					const glm::ivec3 c = world_->sectionCoord(0, *it);
					if (World::cellOf(0, c.x, c.z) != cell)
					{
						++it;
						continue;
					}

					editStore_->put(*it, c, edit::SectionSnapshot::capture(world_->section(0, c.x, c.y, c.z)));
					it = editedSections_.erase(it);
				}

				removeCellBodies(cell);
				std::erase_if(staleColliders_, [&](const glm::ivec3& c) { return World::cellOf(0, c.x, c.z) == cell; });
				collidableCells_.erase(cellKey(cell));
			}

			// evictCell, never an empty mesh: the nodes have to read as "not
			// meshed" for the level above to go on being drawn.
			if (renderer_)
				renderer_->evictCell(level, cell.x, cell.y);

			world_->removePage(level, cell.x, cell.y);
		}

		streamer_->setResident(cell, to);
	}

	/*!
		@brief
			Removes every body, placed or pending, of one cell.
		@param cell
			The cell
		*/
	void System::removeCellBodies(const glm::ivec2& cell)
	{
		for (auto it = bodies_.begin(); it != bodies_.end();)
		{
			const glm::ivec3 c = world_->sectionCoord(0, it->first);
			if (World::cellOf(0, c.x, c.z) != cell)
			{
				++it;
				continue;
			}

			phys_.removeBody(it->second);
			it = bodies_.erase(it);
		}

		std::erase_if(pendingBodies_, [&](const auto& p) { return World::cellOf(0, p.first.x, p.first.z) == cell; });
	}

	/*!
		@brief
			Adds a mesh request for every non-air section of a cell at one level.
		@param cell
			The cell
		@param level
			LoD level
		@param cook
			Whether the jobs cook collision meshes
		@param meshes
			Receives the requests
		*/
	void System::requestCellMeshes(const glm::ivec2& cell, int level, bool cook, std::vector<MeshRequest>& meshes) const
	{
		const int side = World::pageSide(level);
		const int height = world_->sectionsAt(level).y;

		for (int sy = 0; sy < height; ++sy)
		for (int lz = 0; lz < side; ++lz)
		for (int lx = 0; lx < side; ++lx)
		{
			const glm::ivec3 c{ cell.x * side + lx, sy, cell.y * side + lz };
			if (!world_->section(level, c.x, c.y, c.z).isAir())
				meshes.push_back({ level, c, cook });
		}
	}

	/*!
		@brief
			Installs a built cell's pages, takes its kept edits back, and requests
			its meshes plus the border row of adjacent detail-0 cells.
		@param built
			The worker-built sections; moved from
		@param meshes
			Receives the mesh jobs
		*/
	void System::streamInstall(CellSections& built, std::vector<MeshRequest>& meshes)
	{
		const glm::ivec2 cell = built.cell_;
		const int detail = built.detail_;

		// Every level the build kept, the ones already resident included: they
		// were mipped from the edits as they are now.
		for (int level = kLodLevels - 1; level >= detail; --level)
			world_->installPage(level, cell.x, cell.y, std::move(built.sections_[static_cast<std::size_t>(level)]),
				std::move(built.masks_[static_cast<std::size_t>(level)]));

		// The kept edits are back in the world, where an eviction will find them again.
		if (detail == 0)
		{
			for (const StoredSection& kept : editStore_->collect(cell))
			{
				const std::size_t index = world_->sectionIndex(0, kept.section_.x, kept.section_.y, kept.section_.z);
				editedSections_.insert(index);
				editStore_->erase(index);
			}
		}

		// Coarse first, so there is something to draw while the fine meshes
		// come in. Collision is a separate pass (streamColliders).
		for (int level = kLodLevels - 1; level >= detail; --level)
			requestCellMeshes(cell, level, false, meshes);

		// A level-0 section meshes its border against its neighbour. Next to a
		// cell that was not there it meshed a wall; now there is ground, the
		// wall has to go. Coarse levels seal their borders and do not care.
		if (detail == 0)
		{
			const int side = World::pageSide(0);
			const int height = world_->sectionsAt(0).y;
			constexpr glm::ivec2 kSteps[4] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };

			for (const glm::ivec2& step : kSteps)
			{
				const glm::ivec2 other = cell + step;
				if (streamer_->resident(other) != 0)
					continue;

				for (int sy = 0; sy < height; ++sy)
				for (int along = 0; along < side; ++along)
				{
					// The neighbour's row of sections that touches this cell.
					const int lx = step.x != 0 ? (step.x < 0 ? side - 1 : 0) : along;
					const int lz = step.y != 0 ? (step.y < 0 ? side - 1 : 0) : along;
					const glm::ivec3 c{ other.x * side + lx, sy, other.y * side + lz };

					if (!world_->section(0, c.x, c.y, c.z).isAir())
						meshes.push_back({ 0, c, false });
				}
			}
		}

		streamer_->setResident(cell, detail);
	}

	/*!
		@brief
			Drops the bodies of cells that fell behind and, when cooking, requests
			collision meshes for detail-0 cells that came within colliderRadius.
		@param meshes
			Receives the cooking mesh jobs
		*/
	void System::streamColliders(std::vector<MeshRequest>& meshes)
	{
		const stream::Settings& s = streamer_->settings();

		// Behind the player: let the bodies go.
		for (auto it = collidableCells_.begin(); it != collidableCells_.end();)
		{
			const glm::ivec2 cell = cellOfKey(*it);
			if (streamer_->distance(cell) <= s.colliderRadius_ + s.hysteresis_)
			{
				++it;
				continue;
			}

			removeCellBodies(cell);
			it = collidableCells_.erase(it);
		}

		// Ahead: cook. Not while cooking is deferred (the Editor, paused);
		// setColliderMode(Live) comes back here through the same check.
		if (!cookNow())
			return;

		const float cellMetres = static_cast<float>(World::kCellSections * kSectionSize) * world_->dims().voxelSize_;
		const int reach = static_cast<int>(s.colliderRadius_ / cellMetres) + 1;
		const glm::vec3 local = (streamFocus_ - world_->dims().origin_) / cellMetres;
		const glm::ivec2 centre{ static_cast<int>(std::floor(local.x)), static_cast<int>(std::floor(local.z)) };

		for (int cz = centre.y - reach; cz <= centre.y + reach; ++cz)
		for (int cx = centre.x - reach; cx <= centre.x + reach; ++cx)
		{
			const glm::ivec2 cell{ cx, cz };
			if (!world_->validCell(cx, cz) || streamer_->resident(cell) != 0 || collidableCells_.count(cellKey(cell)) != 0)
				continue;
			if (streamer_->distance(cell) > s.colliderRadius_)
				continue;

			// Meshed once already, without a collision mesh; the quads are not
			// kept once uploaded, so cooking means meshing again.
			requestCellMeshes(cell, 0, true, meshes);
			collidableCells_.insert(cellKey(cell));
		}
	}

	/*!
		@brief
			Whether a streamed world has what its first frame needs.
		@return
			true once the detail-0 ring is in and, when cooking, every cell within
			colliderRadius has its collision requested
		*/
	bool System::streamReady() const
	{
		if (!streamer_->ringResident(0))
			return false;

		// The ground under the focus has to be solid before anything is dropped on it.
		if (!cookNow())
			return true;

		const stream::Settings& s = streamer_->settings();
		const float cellMetres = static_cast<float>(World::kCellSections * kSectionSize) * world_->dims().voxelSize_;
		const glm::vec3 local = (streamFocus_ - world_->dims().origin_) / cellMetres;
		const glm::ivec2 centre{ static_cast<int>(std::floor(local.x)), static_cast<int>(std::floor(local.z)) };
		const int reach = static_cast<int>(s.colliderRadius_ / cellMetres) + 1;

		for (int cz = centre.y - reach; cz <= centre.y + reach; ++cz)
		for (int cx = centre.x - reach; cx <= centre.x + reach; ++cx)
		{
			const glm::ivec2 cell{ cx, cz };
			if (world_->validCell(cx, cz) && streamer_->distance(cell) <= s.colliderRadius_ && collidableCells_.count(cellKey(cell)) == 0)
				return false;
		}

		return true;
	}
}
