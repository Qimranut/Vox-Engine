/*************************************************************************************************************
\file       ray.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Rays and the few intersections picking needs (sphere, segment, axis, plane, AABB). GLM only,
            so tools and the Editor can use it without pulling in a subsystem.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// GLM
#include <glm/glm.hpp>

// stds
#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>


namespace util::geom
{
	struct Ray
	{
		glm::vec3	origin_		{ 0.f };
		glm::vec3	direction_	{ 0.f, 0.f, -1.f };	// unit length

		/*!
			@brief
				Point at a distance along the ray.
			@param t
				Distance from the origin
			@return
				origin_ + direction_ * t
			*/
		glm::vec3 at(float t) const { return origin_ + direction_ * t; }
	};

	/*!
		@brief
			The ray through a point of the view, for a perspective camera.

			Built from the camera's basis rather than by inverting the projection:
			with a far plane a thousand times the near one, the inverse loses most
			of a float's precision exactly where picking needs it.
		@param ndc
			Point in normalised device coordinates: x right, y up, both -1..1
		@param eye
			Camera position, the ray's origin
		@param forward
			The camera's unit forward vector
		@param right
			The camera's unit right vector
		@param up
			The camera's unit up vector
		@param fovY
			Vertical field of view in radians
		@param aspect
			Width over height
		@return
			The ray, with a unit direction
		*/
	inline Ray perspectiveRay(const glm::vec2& ndc, const glm::vec3& eye, const glm::vec3& forward,
							  const glm::vec3& right, const glm::vec3& up, float fovY, float aspect)
	{
		const float halfHeight = std::tan(0.5f * fovY);
		const glm::vec3 direction = forward + right * (ndc.x * halfHeight * aspect) + up * (ndc.y * halfHeight);
		return { eye, glm::normalize(direction) };
	}

	/*!
		@brief
			Ray-sphere intersection.
		@param ray
			The ray (unit direction)
		@param centre
			Sphere centre
		@param radius
			Sphere radius
		@return
			Distance along the ray to the nearest hit in front of its origin; a
			ray that starts inside hits at 0
		*/
	inline std::optional<float> raySphere(const Ray& ray, const glm::vec3& centre, float radius)
	{
		const glm::vec3 to = centre - ray.origin_;
		const float along = glm::dot(to, ray.direction_);
		const float off2 = glm::dot(to, to) - along * along;
		const float r2 = radius * radius;
		if (off2 > r2)
			return std::nullopt;

		const float half = std::sqrt(r2 - off2);
		if (along + half < 0.f)
			return std::nullopt;
		return std::max(0.f, along - half);
	}

	/** What closestToSegment() reports. */
	struct SegmentApproach
	{
		float	rayT_		{ 0.f };	// distance along the ray, >= 0
		float	segmentT_	{ 0.f };	// 0 at a, 1 at b
		float	distance_	{ 0.f };	// between the two closest points
	};

	/*!
		@brief
			Where a ray passes closest to the segment a-b.

			Picking a capsule (an arrow's shaft) is this with distance_ <= the
			capsule's radius.
		@param ray
			The ray (unit direction)
		@param a
			Segment start
		@param b
			Segment end
		@return
			Ray and segment parameters of the closest points and their distance
		*/
	inline SegmentApproach closestToSegment(const Ray& ray, const glm::vec3& a, const glm::vec3& b)
	{
		const glm::vec3 d = b - a;
		const glm::vec3 w = ray.origin_ - a;
		const float dd = glm::dot(d, d);
		const float rd = glm::dot(ray.direction_, d);
		const float rw = glm::dot(ray.direction_, w);
		const float dw = glm::dot(d, w);

		// Minimise |origin + t * dir - (a + s * d)| over t >= 0, s in [0, 1].
		const float denom = dd - rd * rd;		// dd * sin^2 of the angle between them
		float s = denom > 1e-8f * std::max(dd, 1e-12f) ? (dw - rd * rw) / denom : 0.f;
		s = std::clamp(s, 0.f, 1.f);

		float t = std::max(0.f, rd * s - rw);

		// t was clamped, so s may have a better answer for that t.
		if (dd > 1e-12f)
			s = std::clamp((dw + t * rd) / dd, 0.f, 1.f);
		t = std::max(0.f, rd * s - rw);

		SegmentApproach out;
		out.rayT_ = t;
		out.segmentT_ = s;
		out.distance_ = glm::length(ray.at(t) - (a + d * s));
		return out;
	}

	/*!
		@brief
			The parameter, along an infinite line, of the line's point nearest to
			a ray: how far a handle has been dragged along its axis.
		@param ray
			The ray (unit direction)
		@param linePoint
			A point on the line, where the parameter is 0
		@param axis
			Unit direction of the line
		@return
			Distance from linePoint along axis; empty when the ray runs so nearly
			parallel to the line that the answer is unstable, and the caller
			should keep the previous one
		*/
	inline std::optional<float> axisParameter(const Ray& ray, const glm::vec3& linePoint, const glm::vec3& axis)
	{
		const glm::vec3 w = ray.origin_ - linePoint;
		const float b = glm::dot(ray.direction_, axis);
		const float denom = 1.f - b * b;
		if (denom < 1e-4f)
			return std::nullopt;

		return (glm::dot(axis, w) - b * glm::dot(ray.direction_, w)) / denom;
	}

	/*!
		@brief
			Where a ray crosses a plane, from either side.
		@param ray
			The ray
		@param planePoint
			Any point on the plane
		@param normal
			Unit normal of the plane
		@return
			Distance along the ray; empty when the plane is behind the origin or
			the ray runs so nearly along it that the crossing is far away and unstable
		*/
	inline std::optional<float> rayPlane(const Ray& ray, const glm::vec3& planePoint, const glm::vec3& normal)
	{
		const float facing = glm::dot(ray.direction_, normal);
		if (std::fabs(facing) < 1e-4f)
			return std::nullopt;

		const float t = glm::dot(planePoint - ray.origin_, normal) / facing;
		if (t < 0.f)
			return std::nullopt;
		return t;
	}

	/*!
		@brief
			Slab test against an axis-aligned box.
		@param ray
			The ray
		@param mn
			Box minimum corner
		@param mx
			Box maximum corner
		@return
			Distance to the entry point, 0 for a ray that starts inside
		*/
	inline std::optional<float> rayAabb(const Ray& ray, const glm::vec3& mn, const glm::vec3& mx)
	{
		float enter = 0.f, leave = 1e30f;	// not near / far: windows.h defines those away
		for (int a = 0; a < 3; ++a)
		{
			const float d = ray.direction_[a];
			if (std::fabs(d) < 1e-9f)
			{
				if (ray.origin_[a] < mn[a] || ray.origin_[a] > mx[a])
					return std::nullopt;
				continue;
			}

			float t0 = (mn[a] - ray.origin_[a]) / d, t1 = (mx[a] - ray.origin_[a]) / d;
			if (t0 > t1)
				std::swap(t0, t1);
			enter = std::max(enter, t0);
			leave = std::min(leave, t1);
			if (enter > leave)
				return std::nullopt;
		}
		return enter;
	}
}
