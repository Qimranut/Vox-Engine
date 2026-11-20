/*************************************************************************************************************
\file       RotateRings.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares RotateRings: three rings around a point, one per axis, dragged to turn a rotation
            about that axis with optional angle snapping. Pure logic over a mouse ray.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Engine
#include "util/geom/ray.hpp"

// GLM
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// stds
#include <optional>
#include <string>


namespace graphics::renderer
{
	class OverlayRenderer;
}

namespace vox::editor
{
	/**
	 * Three rings around a point, one per axis of the thing being turned: drag a ring to turn it
	 * about that axis. Like RegionGizmo it is pure logic over a mouse ray and knows nothing about
	 * ImGui or what it turns, and it checks its own arithmetic because nothing can click it in an
	 * automated run.
	 *
	 */
	class RotateRings
	{
	public:

		struct Input
		{
			util::geom::Ray	ray;
			bool			overViewport	{};
			bool			down			{};
			bool			pressed			{};
			bool			freeAngle		{};			// no snapping while held
			float			fovY			{ 1.f };
			float			viewportHeight	{ 720.f };
		};

		/*!
			@brief
				Picks, starts, continues or ends a ring drag.
			@param rotation
				Turned in place while a ring is dragged
			@param centre
				World position the rings pivot around
			@param radius
				Ring radius, world units
			@param input
				This frame's pointer state
			@param snapDegrees
				Step the angle is rounded to unless freeAngle; 0 for none
			@return
				Whether the rotation changed
			*/
		bool update(
			glm::quat& rotation,
			const glm::vec3& centre,
			float radius,
			const Input& input,
			float snapDegrees);

		/*!
			@brief
				Queues the three rings on the overlay's OnTop layer, lit when hovered or held.
			@param overlay
				Renderer the rings are queued on
			@param rotation
				Current rotation; each ring lies square to one of its axes
			@param centre
				World position the rings pivot around
			@param radius
				Ring radius, world units
			@param input
				This frame's pointer state; its ray origin is the eye
			*/
		void draw(
			graphics::renderer::OverlayRenderer& overlay,
			const glm::quat& rotation,
			const glm::vec3& centre,
			float radius,
			const Input& input) const;

		/*!
			@brief
				Lets go of whatever ring is held or hovered.
			*/
		void release()
		{
			drag_.reset();
			hover_.reset();
		}

		/*!
			@brief
				Whether a ring is under the cursor or being dragged.
			@return
				True while a ring is hovered or held
			*/
		bool	hot() const			{ return hover_.has_value() || drag_.has_value(); }

		/*!
			@brief
				Whether a ring is being dragged.
			@return
				True while a ring is held
			*/
		bool	dragging() const	{ return drag_.has_value(); }

		/*!
			@brief
				Checks picking, dragging past half a turn, snapping, the free-angle key and that an
				edge-on ring cannot be grabbed.
			@return
				Empty when every check passes, else what failed
			*/
		static std::string selfCheck();

	private:

		struct Drag
		{
			int			axis		{};
			glm::vec3	normal		{ 0.f };	// the ring's axis in the world, fixed for the length of the drag
			glm::quat	start		{ 1.f, 0.f, 0.f, 0.f };
			float		startAngle	{};
			float		lastAngle	{};
		};

		/*!
			@brief
				Angle of the ray's crossing of a ring's plane, measured around the ring.
			@param ray
				The mouse ray
			@param centre
				Centre of the ring
			@param normal
				The ring's axis in the world
			@return
				The angle in radians; empty when the ray misses the plane
			*/
		static std::optional<float> angleOn(
			const util::geom::Ray& ray,
			const glm::vec3& centre,
			const glm::vec3& normal);

		/*!
			@brief
				Finds the ring whose plane the ray crosses nearest, within a few pixels of the ring.
			@param rotation
				Current rotation; each ring lies square to one of its axes
			@param centre
				World position the rings pivot around
			@param radius
				Ring radius, world units
			@param input
				Pointer state holding the ray
			@return
				The ring's axis (0 x, 1 y, 2 z), or empty
			*/
		static std::optional<int> pick(
			const glm::quat& rotation,
			const glm::vec3& centre,
			float radius,
			const Input& input);

		std::optional<int>		hover_;
		std::optional<Drag>	drag_;
	};
}
