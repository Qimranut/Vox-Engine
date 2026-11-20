/*************************************************************************************************************
\file       region_transform.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Places a copied block of voxels moved, turned and scaled (nearest-neighbour inverse mapping).
            Optimised in Debug builds too, like brush_ops.cpp: a live Transform re-places its whole
            block every time the box moves.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#if defined(_MSC_VER) && defined(_DEBUG)
#pragma runtime_checks("", off)
#pragma optimize("gt", on)
#endif

#include "region_transform.hpp"

// Profiling
#include "../../profile/profiler.hpp"

// stds
#include <algorithm>
#include <cmath>


namespace voxel::edit
{
	/*!
		@brief
			Every voxel a placed block can reach: the turned, scaled box's world-axis bounds.
		@param source
			The block's source box
		@param placement
			Where and how the block is placed
		@return
			The bounding voxel box
		*/
	VoxelBox placedBounds(const VoxelBox& source, const Placement& placement)
	{
		const glm::vec3 half = 0.5f * glm::vec3(source.size()) * placement.scale_;
		const glm::mat3 rotate = glm::mat3_cast(placement.rotation_);

		// Half extent of the turned box along each world axis.
		glm::vec3 reach{ 0.f };
		for (int axis = 0; axis < 3; ++axis)
			for (int k = 0; k < 3; ++k)
				reach[axis] += std::fabs(rotate[k][axis]) * half[k];

		// Pulled in by a hair: a quarter turn's cosine is not quite zero in
		// floats, and the box must not grow a row of voxels because of it.
		constexpr float kSlack = 1e-3f;
		return { glm::ivec3(glm::floor(placement.centre_ - reach + kSlack)), glm::ivec3(glm::ceil(placement.centre_ + reach - kSlack)) };
	}

	/*!
		@brief
			Writes a placed copy of source into target by mapping each target voxel centre back to a source voxel.
		@param target
			Region to write into
		@param source
			The copied block
		@param placement
			Where and how the block is placed
		@param mergeEmpty
			When set, empty block voxels leave the target alone; otherwise they clear it
		@return
			Voxels changed
		*/
	std::size_t place(VoxelRegion& target, const VoxelRegion& source, const Placement& placement, bool mergeEmpty)
	{
		VOX_PROFILE_FUNCTION_C(profile::colour::kVoxel);

		const VoxelBox& from = source.box();
		if (from.empty())
			return 0;

		const VoxelBox box = placedBounds(from, placement).clipped(target.box());
		const glm::mat3 inverse = glm::transpose(glm::mat3_cast(placement.rotation_));
		const glm::vec3 unscale = 1.f / glm::max(placement.scale_, glm::vec3{ 1e-3f });
		const glm::vec3 sourceCentre = 0.5f * glm::vec3(from.min_ + from.max_);

		// The source point for a target voxel is affine in its coordinates,
		// so one step along x is one addition.
		const glm::vec3 stepX = (inverse * glm::vec3{ 1.f, 0.f, 0.f }) * unscale;

		std::size_t changed = 0;
		for (int y = box.min_.y; y < box.max_.y; ++y)
		for (int z = box.min_.z; z < box.max_.z; ++z)
		{
			const glm::vec3 first{ static_cast<float>(box.min_.x) + 0.5f, static_cast<float>(y) + 0.5f, static_cast<float>(z) + 0.5f };
			glm::vec3 p = (inverse * (first - placement.centre_)) * unscale + sourceCentre;
			MaterialId* row = &target.at(box.min_.x, y, z);

			for (int x = box.min_.x; x < box.max_.x; ++x, p += stepX, ++row)
			{
				// A small nudge keeps an exact quarter turn from landing on a
				// voxel border and rounding the wrong way.
				const glm::ivec3 s = glm::ivec3(glm::floor(p + 1e-4f));
				if (!from.contains(s.x, s.y, s.z))
					continue;

				const MaterialId m = source.at(s.x, s.y, s.z);
				if ((m == kAir && mergeEmpty) || *row == m)
					continue;
				*row = m;
				++changed;
			}
		}
		return changed;
	}
}
