/*************************************************************************************************************
\file       voxel_raycast.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Grid DDA raycast over level 0 of the voxel world that crosses uniformly empty sections in
            one step and can read overriding (earlier) sections in place of the world's.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "voxel_raycast.hpp"

// stds
#include <algorithm>
#include <cmath>
#include <limits>


namespace voxel::edit
{
	namespace
	{
		constexpr double kInfinity = std::numeric_limits<double>::infinity();

		/*!
			@brief
				Distance, in the ray's own units, at which it leaves the cell or block [lo, hi) along one axis.
			@param origin
				Ray origin on this axis
			@param direction
				Ray direction on this axis
			@param lo
				Lower bound of the cell or block
			@param hi
				Upper bound of the cell or block
			@return
				The exit distance; infinity when the ray does not move on this axis
			*/
		double exitAlong(double origin, double direction, double lo, double hi)
		{
			if (direction > 0.0) return (hi - origin) / direction;
			if (direction < 0.0) return (lo - origin) / direction;
			return kInfinity;
		}
	}

	/*!
		@brief
			Clips the ray to the world box and walks level 0 cell by cell, skipping empty uniform sections.
		@param world
			World to read
		@param ray
			Ray in world space
		@param options
			Maximum distance, pass-through material and section overrides
		@return
			The first solid voxel hit, or nullopt
		*/
	std::optional<VoxelHit> raycast(const World& world, const util::geom::Ray& ray, const RaycastOptions& options)
	{
		const WorldDims& dims = world.dims();
		const glm::ivec3 extent = dims.voxels_;

		// Everything below is in voxel units, so a cell is [i, i + 1).
		const glm::dvec3 origin = glm::dvec3(ray.origin_ - dims.origin_) / static_cast<double>(dims.voxelSize_);
		const glm::dvec3 direction = glm::dvec3(ray.direction_);
		const double limit = static_cast<double>(options.maxDistance_) / static_cast<double>(dims.voxelSize_);

		// Clip to the world box, remembering which slab the ray came in through.
		double enter = 0.0, leave = limit;
		int axis = -1;
		for (int a = 0; a < 3; ++a)
		{
			if (std::fabs(direction[a]) < 1e-12)
			{
				if (origin[a] < 0.0 || origin[a] >= static_cast<double>(extent[a]))
					return std::nullopt;
				continue;
			}

			double t0 = (0.0 - origin[a]) / direction[a], t1 = (static_cast<double>(extent[a]) - origin[a]) / direction[a];
			if (t0 > t1)
				std::swap(t0, t1);
			if (t0 > enter) { enter = t0; axis = a; }
			leave = std::min(leave, t1);
			if (enter > leave)
				return std::nullopt;
		}

		const glm::ivec3 step{ direction.x > 0.0 ? 1 : -1, direction.y > 0.0 ? 1 : -1, direction.z > 0.0 ? 1 : -1 };

		// The cell a point at distance t falls in. On a face the point belongs
		// to the cell the ray is heading into.
		const auto cellAt = [&](double t)
		{
			glm::ivec3 cell;
			for (int a = 0; a < 3; ++a)
			{
				const double p = origin[a] + direction[a] * t;
				double f = std::floor(p);
				if (p == f && direction[a] < 0.0)
					f -= 1.0;
				cell[a] = std::clamp(static_cast<int>(f), 0, extent[a] - 1);
			}
			return cell;
		};

		const auto solid = [&](MaterialId m) { return m != kAir && m != options.passThrough_; };

		double t = enter;
		glm::ivec3 cell = cellAt(t + 1e-7);
		glm::dvec3 next;	// distance at which the ray leaves `cell` along each axis
		const auto rearm = [&]
		{
			for (int a = 0; a < 3; ++a)
				next[a] = exitAlong(origin[a], direction[a], cell[a], cell[a] + 1.0);
		};
		rearm();

		const glm::dvec3 perCell{ std::fabs(1.0 / direction.x), std::fabs(1.0 / direction.y), std::fabs(1.0 / direction.z) };

		while (t <= leave)
		{
			const glm::ivec3 sc = cell >> kSectionShift;
			const Section* found = &world.section(0, sc.x, sc.y, sc.z);
			if (options.overrides_ && !options.overrides_->empty())
			{
				const auto earlier = options.overrides_->find(world.sectionIndex(0, sc.x, sc.y, sc.z));
				if (earlier != options.overrides_->end())
					found = &earlier->second;
			}
			const Section& section = *found;

			if (section.isUniform() && !solid(section.uniformMaterial()))
			{
				// Nothing to hit in here: go straight to where the ray leaves the block.
				const glm::ivec3 lo = sc * kSectionSize;
				double out = kInfinity;
				int outAxis = 0;
				for (int a = 0; a < 3; ++a)
				{
					const double e = exitAlong(origin[a], direction[a], lo[a], lo[a] + static_cast<double>(kSectionSize));
					if (e < out) { out = e; outAxis = a; }
				}

				t = out;
				axis = outAxis;
				if (t > leave)
					break;

				cell = cellAt(t);
				cell[outAxis] = step[outAxis] > 0 ? lo[outAxis] + kSectionSize : lo[outAxis] - 1;
				if (cell[outAxis] < 0 || cell[outAxis] >= extent[outAxis])
					break;
				rearm();
				continue;
			}

			const MaterialId m = section.get(cell.x, cell.y, cell.z);
			if (solid(m))
			{
				VoxelHit hit;
				hit.voxel_ = cell;
				hit.material_ = m;
				hit.distance_ = static_cast<float>(t) * dims.voxelSize_;
				hit.point_ = ray.at(hit.distance_);
				if (axis >= 0)
					hit.normal_[axis] = -step[axis];
				return hit;
			}

			axis = next.x <= next.y ? (next.x <= next.z ? 0 : 2) : (next.y <= next.z ? 1 : 2);
			t = next[axis];
			cell[axis] += step[axis];
			if (cell[axis] < 0 || cell[axis] >= extent[axis])
				break;
			next[axis] += perCell[axis];
		}

		return std::nullopt;
	}
}
