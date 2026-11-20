/*************************************************************************************************************
\file       OrientedBoxGizmo.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares OrientedBox and OrientedBoxGizmo: RegionGizmo's arrows and dots on a turned box,
            run in the box's own frame, plus RotateRings around it.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Editor
#include "RegionGizmo.hpp"
#include "RotateRings.hpp"

// GLM
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// stds
#include <string>


namespace vox::editor
{
	/** A box that can also be turned. */
	struct OrientedBox
	{
		glm::vec3	centre		{ 0.f };
		glm::vec3	size		{ 6.f };
		glm::quat	rotation	{ 1.f, 0.f, 0.f, 0.f };

		bool operator==(const OrientedBox&) const = default;
	};

	/**
	 * RegionGizmo's arrows and dots on a box with an orientation, plus RotateRings around it.
	 *
	 * The region gizmo only knows axis-aligned boxes, and stays that way: this moves the mouse ray
	 * into the box's own frame, where the box is axis-aligned, and lets the region gizmo work
	 * there. Its picking is pure ray maths and its drawing goes through the overlay's transform,
	 * so neither can tell. The rings work in world space and need no such trick.
	 *
	 */
	class OrientedBoxGizmo
	{
	public:

		struct Input
		{
			util::geom::Ray	ray;						// world space
			bool			overViewport	{};
			bool			down			{};
			bool			pressed			{};
			bool			freeAngle		{};
			float			fovY			{ 1.f };
			float			viewportHeight	{ 720.f };
		};

		struct Options
		{
			RegionGizmo::Limits	limits;					// sizes and snapping, along the box's own axes
			bool					rings		{ true };	// off while the box may not be turned
			float					snapDegrees	{ 15.f };
		};

		/*!
			@brief
				Runs the rings, then the region gizmo in the box's own frame, for one frame of input.
			@param box
				The box to move, resize or turn
			@param input
				This frame's pointer state, in world space
			@param options
				Limits, snapping and whether the rings are on
			@return
				Whether the box changed
			*/
		bool update(OrientedBox& box, const Input& input, const Options& options);

		/*!
			@brief
				Queues the box and its handles through the overlay's transform, then the rings.
			@param overlay
				Renderer the triangles are queued on
			@param box
				The box to draw
			@param input
				This frame's pointer state, in world space
			@param options
				Whether the rings are drawn
			*/
		void draw(
			graphics::renderer::OverlayRenderer& overlay,
			const OrientedBox& box,
			const Input& input,
			const Options& options) const;

		/*!
			@brief
				Lets go of whatever the handles or rings hold, and unfreezes the frame.
			*/
		void release()
		{
			region_.release();
			rings_.release();
			held_ = false;
		}

		/*!
			@brief
				Whether a handle or ring is hovered or held.
			@return
				True while the mouse is the gizmo's
			*/
		bool	hot() const			{ return region_.hot() || rings_.hot(); }

		/*!
			@brief
				Whether a handle or ring is being dragged.
			@return
				True while a drag is in progress
			*/
		bool	dragging() const	{ return region_.dragging() || rings_.dragging(); }

		/*!
			@brief
				Runs RotateRings' check, then drags the resize dot of a turned box and checks it
				grows along its own axis with the far face fixed.
			@return
				Empty when every check passes, else what failed
			*/
		static std::string selfCheck();

	private:

		/*!
			@brief
				The origin of the box's frame: its rotation about a fixed world point.

				Fixed for the length of a drag, because a resize moves the box's centre and the frame
				must not follow it.
			@param box
				The box
			@return
				The held origin during a drag, else the box's lowest corner
			*/
		glm::vec3 frameOrigin(const OrientedBox& box) const;

		/*!
			@brief
				The radius the rings are drawn at around a box.
			@param box
				The box
			@return
				0.62 times the length of the box's size
			*/
		static float ringRadius(const OrientedBox& box) { return 0.62f * glm::length(box.size); }

		RegionGizmo	region_;
		RotateRings	rings_;
		bool		held_		{};
		glm::vec3	heldOrigin_	{ 0.f };
	};
}
