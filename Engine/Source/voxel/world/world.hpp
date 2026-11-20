/*************************************************************************************************************
\file       world.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      voxel::WorldDims and voxel::World: the voxel grid and its LoD pyramid, stored in per-cell pages
            that a bounded world holds up front and a streamed one installs and removes as cells come and go.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Voxel
#include "../types/voxel_types.hpp"
#include "../types/material_table.hpp"
#include "section.hpp"

// GLM
#include <glm/glm.hpp>

// stds
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_set>
#include <utility>
#include <vector>


namespace util::thread { class JobQueue; }

namespace voxel
{
	/**
	 * Extent and placement of a world. voxels_ is the logical size the
	 * recipe asked for; sections_ is that rounded up to whole sections.
	 * Voxels between the two are always air.
	 *
	 */
	struct WorldDims
	{
		glm::ivec3	voxels_		{ 0 };
		glm::ivec3	sections_	{ 0 };
		float		voxelSize_	{ 0.1f };
		glm::vec3	origin_		{ 0.f };

		/*!
			@brief
				Builds dims from a logical voxel size, rounding sections up.
			@param voxels
				Logical extent in voxels (at least 1 per axis)
			@param voxelSize
				Metres per voxel
			@param origin
				World-space position of voxel (0, 0, 0)'s corner
			@return
				The dims
			*/
		static WorldDims fromVoxels(const glm::ivec3& voxels, float voxelSize, const glm::vec3& origin);
	};

	/**
	 * The voxel grid plus its LoD pyramid. Level 0 is the truth and the
	 * only level a map file stores; levels 1..3 are derived with mipSection
	 * and each carries an 8-bit mask of which children are not empty.
	 *
	 * Storage is paged by CELL: a column of the map kCellSections level-0
	 * sections on a side and as tall as the world, which is exactly one
	 * level-3 section column and everything under it. Each level keeps one
	 * page per cell, holding that cell's sections of that level. A bounded
	 * world has every page from the start and behaves as one flat grid. A
	 * Sparse world starts with none: a streamed terrain installs a cell's
	 * pages when the player comes near and removes them when they leave, and
	 * it may hold a cell's coarse levels without its fine ones.
	 *
	 * A section of an absent page reads as air. Writing one is a bug and
	 * throws: pages only arrive through installPage(), never as a side effect,
	 * because a write that created one would make an evicted cell look
	 * resident (and two workers filling one cell would race to create it).
	 *
	 * Section coordinates, sectionIndex() and sectionCount() describe the
	 * whole logical grid either way, so an index is a stable key whether or
	 * not the section's page is resident.
	 *
	 * Not thread-safe for writes. Worker threads may read sections while
	 * no edit is in flight - voxel::System enforces that ordering - and
	 * pages are installed and removed under the same rule.
	 *
	 */
	class World
	{
	public:

		// Level-0 sections along one side of a cell. 1 << (kLodLevels - 1): a
		// cell is one level-3 section wide, so a cell holds whole subtrees.
		static constexpr int kCellSections = 1 << (kLodLevels - 1);

		enum class Storage : std::uint8_t
		{
			Eager,	// every page allocated up front (air): a bounded world
			Sparse	// none; see installPage()
		};

		/*!
			@brief
				Sizes every level; an Eager world also allocates every page, all air.
			@param dims
				The world's extent and placement
			@param storage
				Eager for a bounded world, Sparse for a streamed one
			*/
		explicit World(const WorldDims& dims, Storage storage = Storage::Eager);

		/*!
			@brief
				The world's extent and placement.
			@return
				The dims
			*/
		const WorldDims& dims() const { return dims_; }

		/*!
			@brief
				How the world's pages are held.
			@return
				Eager or Sparse
			*/
		Storage storage() const { return storage_; }

		/*!
			@brief
				Section grid size at a level.
			@param level
				LoD level
			@return
				Sections per axis
			*/
		glm::ivec3 sectionsAt(int level) const { return levels_[level].dims_; }

		/*!
			@brief
				Sections of the whole logical grid at a level, resident or not.
			@param level
				LoD level
			@return
				The section count
			*/
		std::size_t sectionCount(int level) const
		{
			const glm::ivec3& d = levels_[level].dims_;
			return static_cast<std::size_t>(d.x) * d.y * d.z;
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
		bool validSection(int level, int sx, int sy, int sz) const;

		/*!
			@brief
				The section's index in the whole logical grid (y, then z, then x).
			@param level
				LoD level
			@param sx
				Section x
			@param sy
				Section y
			@param sz
				Section z
			@return
				The index
			*/
		std::size_t sectionIndex(int level, int sx, int sy, int sz) const
		{
			const glm::ivec3& d = levels_[level].dims_;
			return (static_cast<std::size_t>(sy) * d.z + sz) * d.x + sx;
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
		glm::ivec3 sectionCoord(int level, std::size_t index) const;

		/*!
			@brief
				A section to write to.

				Named apart from section() on purpose: through a non-const World an
				overload would pick the writing one for plain reads too, and a read
				of an absent cell must give air, not throw. Throws std::logic_error
				when the section's page is absent.
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
		Section& editSection(int level, int sx, int sy, int sz);

		/*!
			@brief
				Reads a section; air when the section's page is absent.
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
		const Section& section(int level, int sx, int sy, int sz) const
		{
			const Page* page = pageOf(level, sx, sz);
			return page ? page->sections_[slot(level, sx, sy, sz)] : airSection();
		}

		/*!
			@brief
				editSection() by sectionIndex(), for code that walks the grid in file order.
			@param level
				LoD level
			@param index
				The section index
			@return
				The section
			*/
		Section& editSectionAt(int level, std::size_t index);

		/*!
			@brief
				section() by sectionIndex(), for code that walks the grid in file order.
			@param level
				LoD level
			@param index
				The section index
			@return
				The section
			*/
		const Section& sectionAt(int level, std::size_t index) const;

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
		const Section* sectionOrNull(int level, int sx, int sy, int sz) const;

		/*!
			@brief
				Bit childIndex(cx, cy, cz) set iff that child is not uniform air.

				Levels 1..3; level 0, and a section of an absent page, return 0.
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
		std::uint8_t childMask(int level, int sx, int sy, int sz) const;

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
		bool inBounds(int x, int y, int z) const;

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
		MaterialId get(int x, int y, int z) const;

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
		bool set(int x, int y, int z, MaterialId m);

		// ========================= Pages =========================

		/*!
			@brief
				Cells along x and z.
			@return
				The cell counts
			*/
		glm::ivec2 cells() const { return cells_; }

		/*!
			@brief
				The cell a section belongs to.
			@param level
				LoD level
			@param sx
				Section x
			@param sz
				Section z
			@return
				The cell (x, z)
			*/
		static glm::ivec2 cellOf(int level, int sx, int sz)
		{
			return { sx >> (kLodLevels - 1 - level), sz >> (kLodLevels - 1 - level) };
		}

		/*!
			@brief
				Sections along one side of a page at a level: 8, 4, 2, 1.
			@param level
				LoD level
			@return
				The page side
			*/
		static constexpr int pageSide(int level) { return kCellSections >> level; }

		/*!
			@brief
				Sections a page of a level holds: pageSide^2 columns, the level's height.
			@param level
				LoD level
			@return
				The section count
			*/
		std::size_t pageSectionCount(int level) const
		{
			const std::size_t side = static_cast<std::size_t>(pageSide(level));
			return side * side * static_cast<std::size_t>(levels_[level].dims_.y);
		}

		/*!
			@brief
				Where a section sits inside its page, for whoever builds a page to
				install. lx and lz are the section's coordinates within the cell.
			@param level
				LoD level
			@param lx
				Section x within the cell
			@param sy
				Section y
			@param lz
				Section z within the cell
			@return
				The slot index
			*/
		static std::size_t pageSlot(int level, int lx, int sy, int lz)
		{
			const std::size_t side = static_cast<std::size_t>(pageSide(level));
			return (static_cast<std::size_t>(sy) * side + static_cast<std::size_t>(lz)) * side + static_cast<std::size_t>(lx);
		}

		/*!
			@brief
				Whether a cell lies inside the grid.
			@param cx
				Cell x
			@param cz
				Cell z
			@return
				true when inside
			*/
		bool validCell(int cx, int cz) const { return cx >= 0 && cz >= 0 && cx < cells_.x && cz < cells_.y; }

		/*!
			@brief
				Whether a cell's page of a level is resident.
			@param level
				LoD level
			@param cx
				Cell x
			@param cz
				Cell z
			@return
				true when resident
			*/
		bool hasPage(int level, int cx, int cz) const
		{
			return validCell(cx, cz) && levels_[level].pages_[cellIndex(cx, cz)] != nullptr;
		}

		/*!
			@brief
				Whether a section's page is resident. False outside the grid.
			@param level
				LoD level
			@param sx
				Section x
			@param sy
				Section y
			@param sz
				Section z
			@return
				true when resident
			*/
		bool sectionResident(int level, int sx, int sy, int sz) const
		{
			return validSection(level, sx, sy, sz) && pageOf(level, sx, sz) != nullptr;
		}

		/*!
			@brief
				Whether every level-0 section a box of voxels touches is resident.

				The part of the box outside the grid does not count against it.
			@param min
				Voxel coordinates of the box's minimum corner
			@param max
				Voxel coordinates of the box's maximum corner; exclusive
			@return
				true when all of it is resident
			*/
		bool voxelsResident(const glm::ivec3& min, const glm::ivec3& max) const;

		/*!
			@brief
				Puts a cell's sections of one level in place, replacing the page if
				there is one.
			@param level
				LoD level
			@param cx
				Cell x
			@param cz
				Cell z
			@param sections
				pageSectionCount(level) sections laid out by pageSlot(). Slots that
				fall outside the grid (a cell on the far edge) must be air
			@param masks
				One child mask per section; ignored at level 0 and may be empty there
			*/
		void installPage(int level, int cx, int cz, std::vector<Section>&& sections, std::vector<std::uint8_t>&& masks);

		/*!
			@brief
				Drops a cell's page of one level; its sections read as air from then on.
			@param level
				LoD level
			@param cx
				Cell x
			@param cz
				Cell z
			*/
		void removePage(int level, int cx, int cz);

		/*!
			@brief
				Resident pages of a level.
			@param level
				LoD level
			@return
				The page count
			*/
		std::size_t pageCount(int level) const { return levels_[level].resident_; }

		// ========================== LoD ==========================

		/*!
			@brief
				Rebuilds levels 1..3 from level 0. Parallel over parent sections
				when a queue is given. Eager worlds only.
			@param materials
				The table the LoD priorities come from
			@param jobs
				Worker queue, or nullptr to run on this thread
			*/
		void rebuildLevels(const MaterialTable& materials, util::thread::JobQueue* jobs = nullptr);

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
		void remipAncestors(int sx, int sy, int sz, const MaterialTable& materials);

		/*!
			@brief
				The same for a batch of level-0 sections, rebuilding each ancestor
				once however many of its descendants changed.

				A brush stamp touches dozens of sections that share a handful of
				parents. An ancestor whose page is absent is skipped. In a streamed
				world that cannot happen to an edited section: edits are confined
				to cells that hold every level.
			@param sections
				The changed level-0 sections
			@param materials
				The table the LoD priorities come from
			@return
				Every ancestor rebuilt, as (level, section) pairs
			*/
		std::vector<std::pair<int, glm::ivec3>> remipAncestors(std::span<const glm::ivec3> sections, const MaterialTable& materials);

		/*!
			@brief
				Level-0 sections written since the last take; clears the list.
			@return
				The dirty section coordinates
			*/
		std::vector<glm::ivec3> takeDirtyLevel0();

		/*!
			@brief
				Whether any level-0 section was written since the last take.
			@return
				true when there are dirty sections
			*/
		bool hasDirty() const { return !dirty_.empty(); }

		/*!
			@brief
				Marks a level-0 section dirty.
			@param sx
				Section x
			@param sy
				Section y
			@param sz
				Section z
			*/
		void markDirty(int sx, int sy, int sz) { dirty_.insert(sectionIndex(0, sx, sy, sz)); }

		/*!
			@brief
				Dense sections among the resident ones, for stats.
			@param level
				LoD level
			@return
				The dense count
			*/
		std::size_t denseCount(int level) const;

	private:

		struct Page
		{
			std::vector<Section>		sections_;
			std::vector<std::uint8_t>	childMask_;
		};

		struct Level
		{
			glm::ivec3							dims_		{ 0 };
			std::vector<std::unique_ptr<Page>>	pages_;		// cells_.x * cells_.y, null where absent
			std::size_t							resident_	{ 0 };
		};

		/*!
			@brief
				The shared all-air section absent pages read as.
			@return
				The air section
			*/
		static const Section& airSection();

		/*!
			@brief
				A cell's index in a level's page list.
			@param cx
				Cell x
			@param cz
				Cell z
			@return
				The index
			*/
		std::size_t cellIndex(int cx, int cz) const
		{
			return static_cast<std::size_t>(cz) * static_cast<std::size_t>(cells_.x) + static_cast<std::size_t>(cx);
		}

		/*!
			@brief
				The page holding a section; the section must be inside the grid.
			@param level
				LoD level
			@param sx
				Section x
			@param sz
				Section z
			@return
				The page, or nullptr when absent
			*/
		const Page* pageOf(int level, int sx, int sz) const
		{
			const int shift = kLodLevels - 1 - level;
			return levels_[level].pages_[cellIndex(sx >> shift, sz >> shift)].get();
		}

		/*!
			@brief
				The page holding a section, writable; the section must be inside the grid.
			@param level
				LoD level
			@param sx
				Section x
			@param sz
				Section z
			@return
				The page, or nullptr when absent
			*/
		Page* pageOf(int level, int sx, int sz)
		{
			const int shift = kLodLevels - 1 - level;
			return levels_[level].pages_[cellIndex(sx >> shift, sz >> shift)].get();
		}

		/*!
			@brief
				Index of a section within its page.
			@param level
				LoD level
			@param sx
				Section x
			@param sy
				Section y
			@param sz
				Section z
			@return
				The slot index
			*/
		static std::size_t slot(int level, int sx, int sy, int sz)
		{
			const int mask = pageSide(level) - 1;
			return pageSlot(level, sx & mask, sy, sz & mask);
		}

		/*!
			@brief
				Rebuilds one section of a level (and its child mask) from its eight children.
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
		void mipOne(int level, int sx, int sy, int sz, const MaterialTable& materials);

		WorldDims						dims_;
		Storage							storage_	{ Storage::Eager };
		glm::ivec2						cells_		{ 0 };
		std::array<Level, kLodLevels>	levels_;
		std::unordered_set<std::size_t>	dirty_;
	};
}
