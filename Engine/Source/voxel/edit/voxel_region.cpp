/*************************************************************************************************************
\file       voxel_region.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Copies a box of level-0 voxels out of the world and writes it back section by section,
            reporting changed sections and border neighbours. Optimised in Debug builds too, like
            brush_ops.cpp: every stamp of every stroke copies its region out and back through here.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#if defined(_MSC_VER) && defined(_DEBUG)
#pragma runtime_checks("", off)
#pragma optimize("gt", on)
#endif

#include "voxel_region.hpp"

// Profiling
#include "../../profile/profiler.hpp"

// stds
#include <algorithm>
#include <cstring>


namespace voxel::edit
{
	namespace
	{
		/*!
			@brief
				The part of box that lies in one level-0 section, in world voxel coordinates.
			@param box
				Box to clip
			@param section
				Level-0 section coordinate
			@return
				The overlap (may be empty)
			*/
		VoxelBox overlapWithSection(const VoxelBox& box, const glm::ivec3& section)
		{
			const glm::ivec3 lo = section * kSectionSize;
			return box.clipped({ lo, lo + glm::ivec3{ kSectionSize } });
		}
	}

	/*!
		@brief
			Resets out to box and copies the stored part of it from level 0, a row run at a time.
		@param world
			World to read
		@param box
			Voxels to copy
		@param out
			Receives the voxels; outside the world they stay air
		*/
	void read(const World& world, const VoxelBox& box, VoxelRegion& out)
	{
		VOX_PROFILE_FUNCTION_C(profile::colour::kVoxel);

		out.reset(box, kAir);

		const glm::ivec3 grid = world.sectionsAt(0);
		const VoxelBox stored = box.clipped({ glm::ivec3{ 0 }, grid * kSectionSize });
		if (stored.empty())
			return;

		const glm::ivec3 s0 = stored.min_ >> kSectionShift;
		const glm::ivec3 s1 = (stored.max_ - 1) >> kSectionShift;

		for (int sy = s0.y; sy <= s1.y; ++sy)
		for (int sz = s0.z; sz <= s1.z; ++sz)
		for (int sx = s0.x; sx <= s1.x; ++sx)
		{
			const Section& section = world.section(0, sx, sy, sz);
			if (section.isAir())
				continue;

			const VoxelBox part = overlapWithSection(stored, { sx, sy, sz });
			const std::size_t run = static_cast<std::size_t>(part.max_.x - part.min_.x);

			for (int y = part.min_.y; y < part.max_.y; ++y)
			for (int z = part.min_.z; z < part.max_.z; ++z)
			{
				MaterialId* to = &out.at(part.min_.x, y, z);
				if (section.isUniform())
					std::memset(to, section.uniformMaterial(), run);
				else
					std::memcpy(to, section.data() + voxelIndex(part.min_.x, y, z), run);
			}
		}
	}

	/*!
		@brief
			Writes the region into level 0, touching only sections with differing voxels.

			Each changed section is snapshotted through beforeChange, made dense,
			written and collapsed if possible; face neighbours on changed borders
			are reported unless they changed themselves.
		@param world
			World to write to
		@param region
			Voxels to write
		@param beforeChange
			Optional callback run just before a section first changes
		@return
			Changed sections, neighbours to remesh and the voxel count
		*/
	WriteResult write(World& world, const VoxelRegion& region, const BeforeChange& beforeChange)
	{
		VOX_PROFILE_FUNCTION_C(profile::colour::kVoxel);

		WriteResult result;

		// Only what the world stores: the rounding-up voxels past its logical
		// size must stay air, or the mesher would draw them.
		const VoxelBox target = region.box().clipped(VoxelBox::ofWorld(world.dims()));
		if (target.empty())
			return result;

		const glm::ivec3 s0 = target.min_ >> kSectionShift;
		const glm::ivec3 s1 = (target.max_ - 1) >> kSectionShift;

		for (int sy = s0.y; sy <= s1.y; ++sy)
		for (int sz = s0.z; sz <= s1.z; ++sz)
		for (int sx = s0.x; sx <= s1.x; ++sx)
		{
			const glm::ivec3 coord{ sx, sy, sz };
			Section& section = world.editSection(0, sx, sy, sz);
			const VoxelBox part = overlapWithSection(target, coord);
			const int run = part.max_.x - part.min_.x;

			// First find out whether anything differs, and on which borders,
			// without touching the section: most sections a large brush box
			// overlaps are untouched air or buried rock.
			std::size_t differing = 0;
			bool border[6] = {};	// -x +x -y +y -z +z

			for (int y = part.min_.y; y < part.max_.y; ++y)
			for (int z = part.min_.z; z < part.max_.z; ++z)
			{
				const MaterialId* from = &region.at(part.min_.x, y, z);
				const std::uint8_t* have = section.isUniform() ? nullptr : section.data() + voxelIndex(part.min_.x, y, z);
				if (have && std::memcmp(have, from, static_cast<std::size_t>(run)) == 0)
					continue;

				const MaterialId uniform = section.uniformMaterial();
				std::size_t inRow = 0;
				for (int i = 0; i < run; ++i)
				{
					if (from[i] == (have ? have[i] : uniform))
						continue;
					++inRow;

					const int lx = (part.min_.x + i) & kSectionMask;
					if (lx == 0) border[0] = true;
					if (lx == kSectionMask) border[1] = true;
				}
				if (inRow == 0)
					continue;

				differing += inRow;
				const int ly = y & kSectionMask, lz = z & kSectionMask;
				if (ly == 0) border[2] = true;
				if (ly == kSectionMask) border[3] = true;
				if (lz == 0) border[4] = true;
				if (lz == kSectionMask) border[5] = true;
			}

			if (differing == 0)
				continue;

			if (beforeChange)
				beforeChange(coord, section);

			std::uint8_t* dense = section.makeDense();
			for (int y = part.min_.y; y < part.max_.y; ++y)
			for (int z = part.min_.z; z < part.max_.z; ++z)
				std::memcpy(dense + voxelIndex(part.min_.x, y, z), &region.at(part.min_.x, y, z), static_cast<std::size_t>(run));
			section.tryCollapse();

			result.changed_.push_back(coord);
			result.voxelsChanged_ += differing;

			constexpr glm::ivec3 kStep[6] = { { -1, 0, 0 }, { 1, 0, 0 }, { 0, -1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 } };
			for (int n = 0; n < 6; ++n)
			{
				const glm::ivec3 other = coord + kStep[n];
				if (border[n] && world.validSection(0, other.x, other.y, other.z))
					result.neighbours_.push_back(other);
			}
		}

		// A neighbour that changed itself is already in changed_.
		const auto less = [](const glm::ivec3& a, const glm::ivec3& b)
		{
			return a.y != b.y ? a.y < b.y : a.z != b.z ? a.z < b.z : a.x < b.x;
		};
		std::sort(result.neighbours_.begin(), result.neighbours_.end(), less);
		result.neighbours_.erase(std::unique(result.neighbours_.begin(), result.neighbours_.end()), result.neighbours_.end());
		std::erase_if(result.neighbours_, [&](const glm::ivec3& c)
		{
			return std::find(result.changed_.begin(), result.changed_.end(), c) != result.changed_.end();
		});

		return result;
	}
}
