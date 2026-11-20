/*************************************************************************************************************
\file       brush.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Brush shapes and Stamp, one application of a brush in voxel units: coverage, reach, per-row
            spans, falloff and bounds, plus stampFromWorld to build one from metres.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Voxel
#include "../types/voxel_types.hpp"
#include "../world/world.hpp"
#include "voxel_region.hpp"

// GLM
#include <glm/glm.hpp>

// stds
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>


namespace voxel::edit
{
	enum class Shape : std::uint8_t { Sphere, Cube, Cylinder };

	/**
	 * One application of a brush. In voxel units, not metres, so an op never
	 * needs the world's voxel size: centre_ is a point in the voxel grid
	 * (voxel i spans [i, i + 1), so its middle is i + 0.5).
	 *
	 * half_ is the half extent along x, y and z. A sphere only reads x, as
	 * its radius; a cylinder stands upright with radius x and half height y;
	 * a cube uses all three.
	 *
	 */
	struct Stamp
	{
		glm::vec3	centre_	{ 0.f };
		glm::vec3	half_	{ 8.f };
		Shape		shape_	{ Shape::Sphere };

		/*!
			@brief
				The brush radius (half extent along x).
			@return
				The radius in voxels
			*/
		float radius() const { return half_.x; }

		/*!
			@brief
				The half extents that actually bound the shape.
			@return
				Half extents in voxels (a sphere's radius on every axis, a cylinder's radius on x and z)
			*/
		glm::vec3 extent() const
		{
			switch (shape_)
			{
			case Shape::Sphere:		return glm::vec3{ half_.x };
			case Shape::Cylinder:	return { half_.x, half_.y, half_.x };
			default:				return half_;
			}
		}

		/*!
			@brief
				How far a point is from the centre, as a fraction of the brush.
			@param p
				Point in voxel units
			@return
				0 at the centre, 1 on the surface, more outside
			*/
		float reach(const glm::vec3& p) const
		{
			const glm::vec3 d = p - centre_;
			const glm::vec3 e = glm::max(extent(), glm::vec3{ 1e-3f });
			switch (shape_)
			{
			case Shape::Sphere:		return glm::length(d) / e.x;
			case Shape::Cube:		return std::max({ std::fabs(d.x) / e.x, std::fabs(d.y) / e.y, std::fabs(d.z) / e.z });
			default:				return std::max(std::sqrt(d.x * d.x + d.z * d.z) / e.x, std::fabs(d.y) / e.y);
			}
		}

		/*!
			@brief
				Whether the brush covers a point (reach at most 1).
			@param p
				Point in voxel units
			@return
				True when the point is inside the brush
			*/
		bool covers(const glm::vec3& p) const { return reach(p) <= 1.f; }

		/*!
			@brief
				The voxels of one grid line whose centres the brush covers, as [first, last).

				Empty when first >= last. Every shape is convex, so what it covers
				of a line is one run, and finding the run costs one square root
				where testing each voxel costs one per voxel.
			@param axis
				0 for a row along x at voxel (., a, b) = (y, z); 1 for a column along
				y at (a, b) = (x, z)
			@param a
				First fixed coordinate of the line (y for a row, x for a column)
			@param b
				Second fixed coordinate of the line (z)
			@return
				The covered run [first, last) along the line's axis
			*/
		std::pair<int, int> span(int axis, int a, int b) const
		{
			const glm::vec3 e = glm::max(extent(), glm::vec3{ 1e-3f });
			float half = 0.f, middle = 0.f;

			if (axis == 0)
			{
				const float dy = static_cast<float>(a) + 0.5f - centre_.y, dz = static_cast<float>(b) + 0.5f - centre_.z;
				middle = centre_.x;
				switch (shape_)
				{
				case Shape::Sphere:		half = 1.f - (dy * dy + dz * dz) / (e.x * e.x); half = half < 0.f ? -1.f : e.x * std::sqrt(half); break;
				case Shape::Cube:		half = std::fabs(dy) > e.y || std::fabs(dz) > e.z ? -1.f : e.x; break;
				default:				half = 1.f - (dz * dz) / (e.x * e.x); half = half < 0.f || std::fabs(dy) > e.y ? -1.f : e.x * std::sqrt(half); break;
				}
			}
			else
			{
				const float dx = static_cast<float>(a) + 0.5f - centre_.x, dz = static_cast<float>(b) + 0.5f - centre_.z;
				middle = centre_.y;
				switch (shape_)
				{
				case Shape::Sphere:		half = 1.f - (dx * dx + dz * dz) / (e.x * e.x); half = half < 0.f ? -1.f : e.x * std::sqrt(half); break;
				case Shape::Cube:		half = std::fabs(dx) > e.x || std::fabs(dz) > e.z ? -1.f : e.y; break;
				default:				half = dx * dx + dz * dz > e.x * e.x ? -1.f : e.y; break;
				}
			}

			if (half < 0.f)
				return { 0, 0 };
			// Voxel i is covered iff |i + 0.5 - middle| <= half.
			return { static_cast<int>(std::ceil(middle - half - 0.5f)), static_cast<int>(std::floor(middle + half - 0.5f)) + 1 };
		}

		/*!
			@brief
				Weight of a gradual op at a point.

				1 over the inner half of the brush, easing to 0 at its surface, so a
				stroke leaves no step where the brush ended.
			@param p
				Point in voxel units
			@return
				Weight in [0, 1]
			*/
		float falloff(const glm::vec3& p) const
		{
			const float t = std::clamp((reach(p) - 0.5f) * 2.f, 0.f, 1.f);
			return 1.f - t * t * (3.f - 2.f * t);
		}

		/*!
			@brief
				Every voxel whose centre the brush can cover.
			@return
				The bounding voxel box
			*/
		VoxelBox bounds() const
		{
			const glm::vec3 e = extent();
			return { glm::ivec3(glm::floor(centre_ - e)), glm::ivec3(glm::ceil(centre_ + e)) };
		}
	};

	/*!
		@brief
			A stamp from world-space metres.
		@param dims
			World dimensions (origin and voxel size) used to convert to voxel units
		@param centre
			Brush centre in world metres
		@param radius
			Horizontal half extent
		@param halfHeight
			Vertical half extent of a cube or cylinder; a sphere ignores it
		@param shape
			Brush shape
		@return
			The stamp in voxel units
		*/
	inline Stamp stampFromWorld(const WorldDims& dims, const glm::vec3& centre, float radius, float halfHeight, Shape shape)
	{
		Stamp s;
		s.centre_ = (centre - dims.origin_) / dims.voxelSize_;
		s.half_ = glm::vec3{ radius, halfHeight, radius } / dims.voxelSize_;
		s.shape_ = shape;
		return s;
	}
}
