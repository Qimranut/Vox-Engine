/*************************************************************************************************************
\file       world.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      voxel::World: paged section storage, level-0 reads and writes, page install / removal, and
            the LoD pyramid (full rebuild and batched re-mip of edited ancestors).

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "world.hpp"

// Voxel
#include "mipper.hpp"

// Threads
#include "../../util/thread/job_queue.hpp"

// Profiling
#include "../../profile/profiler.hpp"

// stds
#include <atomic>
#include <stdexcept>


namespace voxel
{
	/*!
		@brief
			Builds dims from a logical voxel size, rounding sections up.
		@param voxels
			Logical extent in voxels (clamped to at least 1 per axis)
		@param voxelSize
			Metres per voxel
		@param origin
			World-space position of voxel (0, 0, 0)'s corner
		@return
			The dims
		*/
	WorldDims WorldDims::fromVoxels(const glm::ivec3& voxels, float voxelSize, const glm::vec3& origin)
	{
		WorldDims d;
		d.voxels_ = glm::max(voxels, glm::ivec3{ 1 });
		d.sections_ = (d.voxels_ + (kSectionSize - 1)) / kSectionSize;
		d.voxelSize_ = voxelSize;
		d.origin_ = origin;
		return d;
	}

	/*!
		@brief
			Sizes every level; an Eager world also allocates every page, all air.
		@param dims
			The world's extent and placement
		@param storage
			Eager for a bounded world, Sparse for a streamed one
		*/
	World::World(const WorldDims& dims, Storage storage)
		: dims_(dims)
		, storage_(storage)
	{
		cells_ = {
			(dims_.sections_.x + kCellSections - 1) / kCellSections,
			(dims_.sections_.z + kCellSections - 1) / kCellSections };

		const std::size_t cellCount = static_cast<std::size_t>(cells_.x) * static_cast<std::size_t>(cells_.y);

		glm::ivec3 d = dims_.sections_;
		for (int level = 0; level < kLodLevels; ++level)
		{
			Level& L = levels_[level];
			L.dims_ = d;
			L.pages_.resize(cellCount);
			d = (d + 1) / 2;
		}

		if (storage_ == Storage::Sparse)
			return;

		// A bounded world: every page, all air. A uniform section owns no
		// block, so this is 16 bytes a section until something is written.
		for (int level = 0; level < kLodLevels; ++level)
		{
			Level& L = levels_[level];
			for (std::unique_ptr<Page>& page : L.pages_)
			{
				page = std::make_unique<Page>();
				page->sections_.resize(pageSectionCount(level));
				page->childMask_.assign(pageSectionCount(level), 0);
			}
			L.resident_ = cellCount;
		}
	}

	/*!
		@brief
			The shared all-air section absent pages read as.
		@return
			The air section
		*/
	const Section& World::airSection()
	{
		static const Section air;
		return air;
	}

	/*!
		@brief
			A section to write to; throws std::logic_error when its page is absent.
		@param level
			LoD level
		@param sx
			Section x
		@param sy
			Section y
		@param sz
			Section z
		@return
			The section
		*/
	Section& World::editSection(int level, int sx, int sy, int sz)
	{
		Page* page = pageOf(level, sx, sz);
		if (!page)
			throw std::logic_error("voxel::World: write to a section whose cell is not resident");

		return page->sections_[slot(level, sx, sy, sz)];
	}

	/*!
		@brief
			editSection() by sectionIndex().
		@param level
			LoD level
		@param index
			The section index
		@return
			The section
		*/
	Section& World::editSectionAt(int level, std::size_t index)
	{
		const glm::ivec3 c = sectionCoord(level, index);
		return editSection(level, c.x, c.y, c.z);
	}

	/*!
		@brief
			section() by sectionIndex().
		@param level
			LoD level
		@param index
			The section index
		@return
			The section
		*/
	const Section& World::sectionAt(int level, std::size_t index) const
	{
		const glm::ivec3 c = sectionCoord(level, index);
		return section(level, c.x, c.y, c.z);
	}

	/*!
		@brief
			Whether a level and section coordinate lie inside the grid.
		@param level
			LoD level
		@param sx
			Section x
		@param sy
			Section y
		@param sz
			Section z
		@return
			true when inside
		*/
	bool World::validSection(int level, int sx, int sy, int sz) const
	{
		if (level < 0 || level >= kLodLevels)
			return false;
		const glm::ivec3& d = levels_[level].dims_;
		return sx >= 0 && sy >= 0 && sz >= 0 && sx < d.x && sy < d.y && sz < d.z;
	}

	/*!
		@brief
			The inverse of sectionIndex().
		@param level
			LoD level
		@param index
			The section index
		@return
			The section coordinate
		*/
	glm::ivec3 World::sectionCoord(int level, std::size_t index) const
	{
		const glm::ivec3& d = levels_[level].dims_;
		const int sx = static_cast<int>(index % d.x);
		const std::size_t rest = index / d.x;
		const int sz = static_cast<int>(rest % d.z);
		const int sy = static_cast<int>(rest / d.z);
		return { sx, sy, sz };
	}

	/*!
		@brief
			Reads a section; nullptr outside the grid.
		@param level
			LoD level
		@param sx
			Section x
		@param sy
			Section y
		@param sz
			Section z
		@return
			The section, or nullptr
		*/
	const Section* World::sectionOrNull(int level, int sx, int sy, int sz) const
	{
		return validSection(level, sx, sy, sz) ? &section(level, sx, sy, sz) : nullptr;
	}

	/*!
		@brief
			The mask of a section's non-air children; 0 at level 0, outside the grid or on an absent page.
		@param level
			LoD level
		@param sx
			Section x
		@param sy
			Section y
		@param sz
			Section z
		@return
			The child mask
		*/
	std::uint8_t World::childMask(int level, int sx, int sy, int sz) const
	{
		if (level <= 0 || !validSection(level, sx, sy, sz))
			return 0;

		const Page* page = pageOf(level, sx, sz);
		return page ? page->childMask_[slot(level, sx, sy, sz)] : std::uint8_t{ 0 };
	}

	/*!
		@brief
			Whether a voxel lies inside the logical extent.
		@param x
			Voxel x
		@param y
			Voxel y
		@param z
			Voxel z
		@return
			true when inside
		*/
	bool World::inBounds(int x, int y, int z) const
	{
		return x >= 0 && y >= 0 && z >= 0 && x < dims_.voxels_.x && y < dims_.voxels_.y && z < dims_.voxels_.z;
	}

	/*!
		@brief
			Level-0 read; air outside the grid.
		@param x
			Voxel x
		@param y
			Voxel y
		@param z
			Voxel z
		@return
			The material
		*/
	MaterialId World::get(int x, int y, int z) const
	{
		if (!inBounds(x, y, z))
			return kAir;
		return section(0, x >> kSectionShift, y >> kSectionShift, z >> kSectionShift).get(x, y, z);
	}

	/*!
		@brief
			Level-0 write. Marks the section dirty.
		@param x
			Voxel x
		@param y
			Voxel y
		@param z
			Voxel z
		@param m
			The material to write
		@return
			false when out of range, unchanged, or the section is not resident
		*/
	bool World::set(int x, int y, int z, MaterialId m)
	{
		if (!inBounds(x, y, z))
			return false;

		const int sx = x >> kSectionShift, sy = y >> kSectionShift, sz = z >> kSectionShift;

		Page* page = pageOf(0, sx, sz);
		if (!page || !page->sections_[slot(0, sx, sy, sz)].set(x, y, z, m))
			return false;

		dirty_.insert(sectionIndex(0, sx, sy, sz));
		return true;
	}

	/*!
		@brief
			Rebuilds one section of a level (and its child mask) from its eight
			children; skipped when the section's page is absent.
		@param level
			LoD level, 1..3
		@param sx
			Section x
		@param sy
			Section y
		@param sz
			Section z
		@param materials
			The table the LoD priorities come from
		*/
	void World::mipOne(int level, int sx, int sy, int sz, const MaterialTable& materials)
	{
		const Section* children[8];
		std::uint8_t mask = 0;

		for (int cx = 0; cx < 2; ++cx)
		for (int cy = 0; cy < 2; ++cy)
		for (int cz = 0; cz < 2; ++cz)
		{
			const int k = childIndex(cx, cy, cz);
			const Section* c = sectionOrNull(level - 1, 2 * sx + cx, 2 * sy + cy, 2 * sz + cz);
			children[k] = c;
			if (c && !c->isAir())
				mask |= static_cast<std::uint8_t>(1u << k);
		}

		// Absent: a coarse level the cell does not hold. Nothing to rebuild.
		Page* page = pageOf(level, sx, sz);
		if (!page)
			return;

		const std::size_t at = slot(level, sx, sy, sz);
		page->sections_[at] = mipSection(children, materials);
		page->childMask_[at] = mask;
	}

	/*!
		@brief
			Whether every level-0 page a box of voxels touches is resident.
		@param min
			Voxel coordinates of the box's minimum corner
		@param max
			Voxel coordinates of the box's maximum corner; exclusive
		@return
			true when all of it is resident
		*/
	bool World::voxelsResident(const glm::ivec3& min, const glm::ivec3& max) const
	{
		const glm::ivec3 lo = glm::max(min, glm::ivec3{ 0 }) >> kSectionShift;
		const glm::ivec3 hi = (glm::min(max, dims_.sections_ * kSectionSize) - 1) >> kSectionShift;

		// Pages are whole columns, so only x and z decide.
		const glm::ivec2 c0 = cellOf(0, lo.x, lo.z);
		const glm::ivec2 c1 = cellOf(0, hi.x, hi.z);

		for (int cz = c0.y; cz <= c1.y; ++cz)
		for (int cx = c0.x; cx <= c1.x; ++cx)
			if (!hasPage(0, cx, cz))
				return false;

		return true;
	}

	/*!
		@brief
			Puts a cell's sections of one level in place, replacing the page if
			there is one; throws on a bad cell or section count.
		@param level
			LoD level
		@param cx
			Cell x
		@param cz
			Cell z
		@param sections
			pageSectionCount(level) sections laid out by pageSlot()
		@param masks
			One child mask per section; padded with zeros when short
		*/
	void World::installPage(int level, int cx, int cz, std::vector<Section>&& sections, std::vector<std::uint8_t>&& masks)
	{
		if (level < 0 || level >= kLodLevels || !validCell(cx, cz))
			throw std::out_of_range("voxel::World::installPage: no such cell");
		if (sections.size() != pageSectionCount(level))
			throw std::invalid_argument("voxel::World::installPage: wrong section count for the level");

		masks.resize(sections.size(), 0);

		Level& L = levels_[level];
		std::unique_ptr<Page>& page = L.pages_[cellIndex(cx, cz)];
		if (!page)
		{
			page = std::make_unique<Page>();
			++L.resident_;
		}

		page->sections_ = std::move(sections);
		page->childMask_ = std::move(masks);
	}

	/*!
		@brief
			Drops a cell's page of one level; at level 0 also clears the dirty
			marks of its sections.
		@param level
			LoD level
		@param cx
			Cell x
		@param cz
			Cell z
		*/
	void World::removePage(int level, int cx, int cz)
	{
		if (level < 0 || level >= kLodLevels || !validCell(cx, cz))
			return;

		Level& L = levels_[level];
		std::unique_ptr<Page>& page = L.pages_[cellIndex(cx, cz)];
		if (!page)
			return;

		page.reset();
		--L.resident_;

		if (level != 0)
			return;

		// A dirty mark on a section that is gone would be taken for an edit.
		const int side = pageSide(0);
		for (int sy = 0; sy < L.dims_.y; ++sy)
		for (int lz = 0; lz < side; ++lz)
		for (int lx = 0; lx < side; ++lx)
		{
			const int sx = cx * side + lx, sz = cz * side + lz;
			if (validSection(0, sx, sy, sz))
				dirty_.erase(sectionIndex(0, sx, sy, sz));
		}
	}

	/*!
		@brief
			Rebuilds levels 1..3 from level 0, one job per row of parents when a
			queue is given. Throws std::logic_error for a Sparse world.
		@param materials
			The table the LoD priorities come from
		@param jobs
			Worker queue, or nullptr to run on this thread
		*/
	void World::rebuildLevels(const MaterialTable& materials, util::thread::JobQueue* jobs)
	{
		VOX_PROFILE_FUNCTION_C(profile::colour::kVoxel);

		// Rows of parents run side by side; that is only safe when no page can
		// be missing, and a streamed cell arrives with its levels already built.
		if (storage_ != Storage::Eager)
			throw std::logic_error("voxel::World::rebuildLevels: only for a world that holds every page");

		for (int level = 1; level < kLodLevels; ++level)
		{
			const glm::ivec3 d = levels_[level].dims_;

			if (jobs == nullptr)
			{
				for (int sy = 0; sy < d.y; ++sy)
				for (int sz = 0; sz < d.z; ++sz)
				for (int sx = 0; sx < d.x; ++sx)
					mipOne(level, sx, sy, sz, materials);
				continue;
			}

			// One job per row of parent sections; rows write disjoint slots
			// and only read the finished level below.
			for (int sy = 0; sy < d.y; ++sy)
			for (int sz = 0; sz < d.z; ++sz)
			{
				jobs->push([this, level, sy, sz, d, &materials]
				{
					for (int sx = 0; sx < d.x; ++sx)
						mipOne(level, sx, sy, sz, materials);
				});
			}
			jobs->waitIdle();
		}
	}

	/*!
		@brief
			Re-mips the ancestors of one level-0 section after an edit.
		@param sx
			Level-0 section x
		@param sy
			Level-0 section y
		@param sz
			Level-0 section z
		@param materials
			The table the LoD priorities come from
		*/
	void World::remipAncestors(int sx, int sy, int sz, const MaterialTable& materials)
	{
		VOX_PROFILE_FUNCTION_C(profile::colour::kVoxel);

		for (int level = 1; level < kLodLevels; ++level)
		{
			sx >>= 1; sy >>= 1; sz >>= 1;
			if (!validSection(level, sx, sy, sz))
				return;
			mipOne(level, sx, sy, sz, materials);
		}
	}

	/*!
		@brief
			Re-mips the ancestors of a batch of level-0 sections, level by level,
			each ancestor once; ancestors on absent pages are skipped.
		@param sections
			The changed level-0 sections
		@param materials
			The table the LoD priorities come from
		@return
			Every ancestor rebuilt, as (level, section) pairs
		*/
	std::vector<std::pair<int, glm::ivec3>> World::remipAncestors(std::span<const glm::ivec3> sections, const MaterialTable& materials)
	{
		VOX_PROFILE_FUNCTION_C(profile::colour::kVoxel);

		std::vector<std::pair<int, glm::ivec3>> rebuilt;

		// Level by level, because a parent is built from its finished children.
		std::unordered_set<std::size_t> below;
		for (const glm::ivec3& s : sections)
			if (validSection(0, s.x, s.y, s.z))
				below.insert(sectionIndex(0, s.x, s.y, s.z));

		for (int level = 1; level < kLodLevels && !below.empty(); ++level)
		{
			std::unordered_set<std::size_t> here;
			for (const std::size_t index : below)
			{
				const glm::ivec3 parent = sectionCoord(level - 1, index) >> 1;
				if (validSection(level, parent.x, parent.y, parent.z))
					here.insert(sectionIndex(level, parent.x, parent.y, parent.z));
			}

			for (const std::size_t index : here)
			{
				const glm::ivec3 c = sectionCoord(level, index);
				if (!pageOf(level, c.x, c.z))
					continue;

				mipOne(level, c.x, c.y, c.z, materials);
				rebuilt.emplace_back(level, c);
			}
			below = std::move(here);
		}

		return rebuilt;
	}

	/*!
		@brief
			Level-0 sections written since the last take; clears the list.
		@return
			The dirty section coordinates
		*/
	std::vector<glm::ivec3> World::takeDirtyLevel0()
	{
		std::vector<glm::ivec3> out;
		out.reserve(dirty_.size());
		for (const std::size_t index : dirty_)
			out.push_back(sectionCoord(0, index));
		dirty_.clear();
		return out;
	}

	/*!
		@brief
			Dense sections among the resident ones, for stats.
		@param level
			LoD level
		@return
			The dense count
		*/
	std::size_t World::denseCount(int level) const
	{
		std::size_t n = 0;
		for (const std::unique_ptr<Page>& page : levels_[level].pages_)
		{
			if (!page)
				continue;

			for (const Section& s : page->sections_)
				if (!s.isUniform())
					++n;
		}
		return n;
	}
}
