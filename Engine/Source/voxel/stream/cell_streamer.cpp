/*************************************************************************************************************
\file       cell_streamer.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Implements CellStreamer: loading stream.json, choosing each cell's detail by distance,
            scheduling tile and cell builds on its workers, and handing finished cells back nearest first.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "cell_streamer.hpp"

// Resources
#include "../../resources/ResourceManager.hpp"

// JSON
#include <nlohmann/json.hpp>

// Profiling
#include "../../profile/profiler.hpp"

// stds
#include <algorithm>
#include <cmath>
#include <iostream>
#include <thread>
#include <utility>


namespace voxel::stream
{
	namespace
	{
		// Cells being built at once, per worker: enough that a worker never
		// waits for the next frame's schedule(), few enough that what is queued
		// is still what is wanted when its turn comes.
		constexpr std::size_t kInFlightPerWorker = 2;

		/*!
			@brief
				Resolves the worker thread count: the asked number, or half the cores clamped to 2..8.
			@param asked
				Configured count; 0 picks automatically
			@return
				Number of workers
			*/
		unsigned workerCount(unsigned asked)
		{
			if (asked != 0)
				return asked;

			const unsigned cores = std::thread::hardware_concurrency();
			return std::clamp(cores / 2, 2u, 8u);
		}
	}

	/*!
		@brief
			Reads voxel/stream.json, clamping each value; a missing or broken file logs and keeps the defaults.
		@param resources
			Resource manager to load the file through
		@return
			The settings
		*/
	Settings Settings::load(vox::ResourceManager& resources)
	{
		Settings s;

		try
		{
			const vox::ResourceHandle file = resources.load("voxel/stream.json");
			const nlohmann::json doc = nlohmann::json::parse(file->bytes.begin(), file->bytes.end());

			if (const auto radii = doc.find("radius"); radii != doc.end() && radii->is_array())
				for (std::size_t i = 0; i < s.radius_.size() && i < radii->size(); ++i)
					s.radius_[i] = (*radii)[i].get<float>();

			// Each ring at least as wide as the finer one inside it.
			s.radius_[0] = std::max(s.radius_[0], 16.f);
			for (std::size_t i = 1; i < s.radius_.size(); ++i)
				s.radius_[i] = std::max(s.radius_[i], s.radius_[i - 1]);

			s.hysteresis_ = std::max(0.f, doc.value("hysteresis", s.hysteresis_));
			s.colliderRadius_ = std::clamp(doc.value("colliderRadius", s.colliderRadius_), 8.f, s.radius_[0]);
			s.poolBudgetBytes_ = static_cast<std::size_t>(std::max(64, doc.value("poolBudgetMiB", 512))) << 20;
			s.editStoreBytes_ = static_cast<std::size_t>(std::max(1, doc.value("editStoreMiB", 64))) << 20;
			s.tileCacheTiles_ = static_cast<std::size_t>(std::max(2, doc.value("tileCacheTiles", static_cast<int>(s.tileCacheTiles_))));
			s.workers_ = static_cast<unsigned>(std::max(0, doc.value("workers", 0)));
			s.installJobsEditing_ = std::max(1, doc.value("installJobsWhileEditing", s.installJobsEditing_));
			s.installJobs_ = std::max(s.installJobsEditing_, doc.value("installJobs", s.installJobs_));
			s.arenaBytes_ = static_cast<std::size_t>(std::max(32, doc.value("quadArenaMiB", 192))) << 20;
		}
		catch (const std::exception& e)
		{
			std::cerr << "[voxel] stream.json: " << e.what() << " - using defaults\n";
			s = Settings{};
		}

		return s;
	}

	/*!
		@brief
			Sets up the tile generator, tile cache and workers, and derives the cell grid from the recipe.
		@param recipe
			Endless version-3 recipe
		@param materials
			Material table
		@param settings
			Streaming settings
		*/
	CellStreamer::CellStreamer(const Recipe& recipe, const MaterialTable& materials, const Settings& settings)
		: settings_(settings)
		, generator_(recipe, materials)
		, tiles_(settings.tileCacheTiles_)
		, jobs_(workerCount(settings.workers_))
	{
		const WorldDims dims = generator_.dims();
		cellCount_ = { dims.sections_.x / World::kCellSections, dims.sections_.z / World::kCellSections };
		cellMetres_ = static_cast<float>(World::kCellSections * kSectionSize) * dims.voxelSize_;
	}

	/*!
		@brief
			Raises the cancel flag so running jobs stop early; the job queue then waits for them.
		*/
	CellStreamer::~CellStreamer()
	{
		// A tile polls this between passes; the queue's destructor then only
		// has the jobs that are running to wait for.
		cancel_.store(true);
	}

	/*!
		@brief
			Distance in metres from the focus to the nearest point of a cell, in the ground plane.
		@param cell
			Cell coordinate
		@return
			Distance in metres
		*/
	float CellStreamer::distance(const glm::ivec2& cell) const
	{
		const glm::vec3 origin = generator_.dims().origin_;
		const glm::vec2 lo = glm::vec2(origin.x, origin.z) + glm::vec2(cell) * cellMetres_;
		const glm::vec2 p{ focus_.x, focus_.z };
		const glm::vec2 nearest = glm::clamp(p, lo, lo + cellMetres_);
		return glm::length(p - nearest);
	}

	/*!
		@brief
			The detail a cell at this distance should have; grace widens every ring.
		@param distance
			Distance from the focus in metres
		@param grace
			Metres added to every scaled ring radius
		@return
			The wanted detail, or kAbsent beyond the last ring
		*/
	int CellStreamer::wanted(float distance, float grace) const
	{
		for (int detail = 0; detail < kLodLevels; ++detail)
			if (distance <= settings_.radius_[static_cast<std::size_t>(detail)] * radiusScale_ + grace)
				return detail;

		return kAbsent;
	}

	/*!
		@brief
			Calls visit for every cell within the outermost ring's square reach of the focus, clipped to the grid.
		@param visit
			Called as visit(glm::ivec2 cell)
		*/
	template <typename Visit>
	void CellStreamer::forCellsInRange(Visit&& visit) const
	{
		const glm::vec3 origin = generator_.dims().origin_;
		const float reach = settings_.radius_[kLodLevels - 1] * radiusScale_;

		const auto cellAt = [&](float metres, float from) { return static_cast<int>(std::floor((metres - from) / cellMetres_)); };
		const int x0 = std::max(cellAt(focus_.x - reach, origin.x), 0), x1 = std::min(cellAt(focus_.x + reach, origin.x), cellCount_.x - 1);
		const int z0 = std::max(cellAt(focus_.z - reach, origin.z), 0), z1 = std::min(cellAt(focus_.z + reach, origin.z), cellCount_.y - 1);

		for (int cz = z0; cz <= z1; ++cz)
		for (int cx = x0; cx <= x1; ++cx)
			visit(glm::ivec2{ cx, cz });
	}

	/*!
		@brief
			Queues tile and cell builds for cells that should be finer, nearest first, within worker room.

			Also updates idle() and the tile cache's focus. Tiles never take
			every worker, so cells whose tile is ready keep coming.
		@param edits
			What to put in place of generated sections; copied per cell
		*/
	void CellStreamer::schedule(const EditStore& edits)
	{
		VOX_PROFILE_SCOPE_C("Stream schedule", profile::colour::kVoxel);

		struct Candidate
		{
			glm::ivec2	cell_;
			int			detail_;
			float		distance_;
		};

		std::vector<Candidate> candidates;
		bool behind = false;

		forCellsInRange([&](const glm::ivec2& cell)
		{
			const float d = distance(cell);
			const int want = wanted(d, 0.f);
			if (want == kAbsent)
				return;

			const auto found = cells_.find(key(cell));
			const int have = found == cells_.end() ? kAbsent : found->second.resident_;
			if (want >= have)
				return;

			behind = true;

			// Already on its way at least this fine.
			if (found != cells_.end() && found->second.building_ >= 0 && found->second.building_ <= want)
				return;

			candidates.push_back({ cell, want, d });
		});

		{
			std::lock_guard<std::mutex> lock(finishedMutex_);
			idle_ = !behind && inFlight_ == 0 && tilesBuilding_.load() == 0 && finished_.empty();
		}

		// Tiles are kept by distance from the focus.
		{
			const glm::vec3 origin = generator_.dims().origin_;
			const float tileMetres = cellMetres_ * static_cast<float>(generator_.tileCells());
			tiles_.setFocus({ static_cast<int>(std::floor((focus_.x - origin.x) / tileMetres)),
				static_cast<int>(std::floor((focus_.z - origin.z) / tileMetres)) });
		}

		// A tile is seconds of work and a cell milliseconds: tiles never take
		// every worker, so cells whose tile is ready keep coming.
		const unsigned workers = jobs_.workerCount();
		const std::size_t room = kInFlightPerWorker * workers;
		const std::size_t tileRoom = std::max(1u, workers - 1);

		const auto full = [&] { return inFlight_ >= room && tilesBuilding_.load() >= tileRoom; };
		if (candidates.empty() || full())
			return;

		// Nearest first; among equals the finer, which is the one in view.
		std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b)
		{
			return a.distance_ != b.distance_ ? a.distance_ < b.distance_ : a.detail_ < b.detail_;
		});

		for (const Candidate& c : candidates)
		{
			if (full())
				break;

			const glm::ivec2 tileAt = generator_.tileOfCell(c.cell_);
			std::shared_ptr<const Tile> tile = inFlight_ < room ? tiles_.ready(tileAt) : nullptr;

			if (!tile)
			{
				// Being built already, or no room: this cell waits for a later call.
				if (tilesBuilding_.load() >= tileRoom || !tiles_.reserve(tileAt))
					continue;

				tilesBuilding_.fetch_add(1);
				jobs_.push([this, tileAt]
				{
					try
					{
						tiles_.build(generator_, tileAt, &cancel_);
					}
					catch (const std::exception& e)
					{
						std::cerr << "[voxel] streaming tile (" << tileAt.x << ", " << tileAt.y << ") failed: " << e.what() << "\n";
					}

					tilesBuilding_.fetch_sub(1);
				});
				continue;
			}

			State& state = cells_[key(c.cell_)];
			state.building_ = static_cast<std::int8_t>(c.detail_);
			state.ticket_ = nextTicket_++;
			++inFlight_;

			jobs_.push([this, tile = std::move(tile), cell = c.cell_, detail = c.detail_, ticket = state.ticket_, kept = edits.collect(c.cell_)]
			{
				Finished result;
				result.ticket_ = ticket;
				result.cell_.cell_ = cell;
				result.cell_.detail_ = kAbsent;		// "nothing built", unless the lines below get there

				try
				{
					if (!cancel_.load())
						result.cell_ = generator_.buildCell(*tile, cell, detail, kept);
				}
				catch (const std::exception& e)
				{
					std::cerr << "[voxel] streaming cell (" << cell.x << ", " << cell.y << ") failed: " << e.what() << "\n";
				}

				std::lock_guard<std::mutex> lock(finishedMutex_);
				finished_.push_back(std::move(result));
			});
		}
	}

	/*!
		@brief
			Cells that hold finer levels than their distance, with hysteresis, now calls for.
		@param keepFinest
			Leave detail-0 cells alone while an edit transaction is open
		@return
			One eviction per such cell
		*/
	std::vector<CellStreamer::Eviction> CellStreamer::evictions(bool keepFinest) const
	{
		std::vector<Eviction> out;

		for (const auto& [k, state] : cells_)
		{
			if (state.resident_ == kAbsent)
				continue;

			const glm::ivec2 cell{ static_cast<int>(static_cast<std::uint32_t>(k)), static_cast<int>(static_cast<std::uint32_t>(k >> 32)) };
			const int keep = wanted(distance(cell), settings_.hysteresis_);
			if (keep <= state.resident_)
				continue;

			if (keepFinest && state.resident_ == 0)
				continue;

			out.push_back({ cell, state.resident_, keep });
		}

		return out;
	}

	/*!
		@brief
			Takes finished cells nearest first under a section budget, dropping stale or unwanted results.
		@param budgetSections
			Non-air sections to hand out before stopping; always hands out at least one cell
		@return
			The cells to install
		*/
	std::vector<CellSections> CellStreamer::takeFinished(std::size_t budgetSections)
	{
		std::vector<Finished> finished;
		{
			std::lock_guard<std::mutex> lock(finishedMutex_);
			if (finished_.empty())
				return {};

			// Nearest first: what the player is walking into goes in before
			// what is on the horizon.
			std::sort(finished_.begin(), finished_.end(), [this](const Finished& a, const Finished& b)
			{
				return distance(a.cell_.cell_) < distance(b.cell_.cell_);
			});

			// Sections that will be meshed: a page's air slots cost nothing,
			// and a detail-0 cell is ~1000 slots but ~150 dense sections.
			std::size_t sections = 0, count = 0;
			while (count < finished_.size() && (count == 0 || sections < budgetSections))
			{
				for (const auto& level : finished_[count].cell_.sections_)
					sections += static_cast<std::size_t>(std::count_if(level.begin(), level.end(), [](const Section& s) { return !s.isAir(); }));
				++count;
			}

			finished.assign(std::make_move_iterator(finished_.begin()), std::make_move_iterator(finished_.begin() + static_cast<std::ptrdiff_t>(count)));
			finished_.erase(finished_.begin(), finished_.begin() + static_cast<std::ptrdiff_t>(count));
		}

		std::vector<CellSections> out;
		for (Finished& f : finished)
		{
			--inFlight_;

			const auto found = cells_.find(key(f.cell_.cell_));
			if (found == cells_.end())
				continue;

			State& state = found->second;
			const bool current = state.ticket_ == f.ticket_;
			if (current)
				state.building_ = -1;

			// Superseded, failed or cancelled, no longer wanted this fine, or
			// the focus has gone: the sections go back to the pool.
			if (!current || f.cell_.detail_ >= state.resident_ || wanted(distance(f.cell_.cell_), settings_.hysteresis_) > f.cell_.detail_)
			{
				if (state.resident_ == kAbsent && state.building_ < 0)
					cells_.erase(found);
				continue;
			}

			out.push_back(std::move(f.cell_));
		}

		return out;
	}

	/*!
		@brief
			Records a cell's resident detail; kAbsent forgets the cell unless it is being built.
		@param cell
			Cell coordinate
		@param detail
			Finest level now resident, or kAbsent
		*/
	void CellStreamer::setResident(const glm::ivec2& cell, int detail)
	{
		if (detail != kAbsent)
		{
			cells_[key(cell)].resident_ = static_cast<std::int8_t>(detail);
			return;
		}

		const auto found = cells_.find(key(cell));
		if (found == cells_.end())
			return;

		found->second.resident_ = static_cast<std::int8_t>(kAbsent);
		if (found->second.building_ < 0)
			cells_.erase(found);
	}

	/*!
		@brief
			The finest level a cell holds.
		@param cell
			Cell coordinate
		@return
			The resident detail, or kAbsent
		*/
	int CellStreamer::resident(const glm::ivec2& cell) const
	{
		const auto found = cells_.find(key(cell));
		return found == cells_.end() ? kAbsent : found->second.resident_;
	}

	/*!
		@brief
			Marks the build in flight for a cell out of date, so its result is dropped on arrival.
		@param cell
			Cell coordinate
		*/
	void CellStreamer::invalidate(const glm::ivec2& cell)
	{
		// A ticket no job holds: the result in flight is dropped on arrival,
		// and the next schedule() asks again, with the edits as they are now.
		if (const auto found = cells_.find(key(cell)); found != cells_.end() && found->second.building_ >= 0)
		{
			found->second.ticket_ = nextTicket_++;
			found->second.building_ = -1;
		}
	}

	/*!
		@brief
			Whether every cell that should be at this detail or finer, is.
		@param detail
			LoD level of the ring
		@return
			true when the ring is resident
		*/
	bool CellStreamer::ringResident(int detail) const
	{
		bool all = true;
		forCellsInRange([&](const glm::ivec2& cell)
		{
			if (wanted(distance(cell), 0.f) <= detail && resident(cell) > detail)
				all = false;
		});
		return all;
	}

	/*!
		@brief
			Collects cell counts by detail, cells building and the tile cache's figures.
		@return
			The stats
		*/
	CellStreamer::Stats CellStreamer::stats() const
	{
		Stats s;
		for (const auto& [k, state] : cells_)
			if (state.resident_ != kAbsent)
				++s.cells_[static_cast<std::size_t>(state.resident_)];

		s.building_ = static_cast<std::uint32_t>(inFlight_);
		s.tiles_ = tiles_.size();
		s.tileBytes_ = tiles_.bytes();
		s.tileBuilds_ = tiles_.builds();
		s.tileHits_ = tiles_.hits();
		s.radiusScale_ = radiusScale_;
		return s;
	}
}
