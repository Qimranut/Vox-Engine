/*************************************************************************************************************
\file       RegionGizmo.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares TerrainRegion, the box terrain is generated in, and RegionGizmo, the viewport
            handles that move and resize it: pure logic over a mouse ray, drawn on an OverlayRenderer.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Engine
#include "util/geom/ray.hpp"

// GLM
#include <glm/glm.hpp>

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
	 * An axis-aligned box in world space, in metres: where terrain will be generated.
	 *
	 */
	struct TerrainRegion
	{
		glm::vec3	min_	{ 0.f };
		glm::vec3	size_	{ 100.f, 38.4f, 100.f };

		/*!
			@brief
				The box's highest corner.
			@return
				min_ + size_, in metres
			*/
		glm::vec3	max() const		{ return min_ + size_; }

		/*!
			@brief
				The box's centre.
			@return
				The midpoint of the box, in metres
			*/
		glm::vec3	centre() const	{ return min_ + 0.5f * size_; }

		bool operator==(const TerrainRegion&) const = default;
	};

	/**
	 * The handles that place a TerrainRegion in the viewport: on each of the
	 * box's six faces an arrow that moves the box along that face's axis, and
	 * a dot that moves the face alone (the opposite face stays put). Handles
	 * keep one size on screen whatever the distance.
	 *
	 * Pure logic: it is given a mouse ray and the button state, changes the
	 * region, and queues its drawing on an OverlayRenderer. It knows nothing
	 * of ImGui, GLFW or the camera classes, which is what lets selfCheck()
	 * exercise picking and dragging without a window.
	 *
	 */
	class RegionGizmo
	{
	public:

		/**
		 * One frame of pointer state, already in world space.
		 *
		 */
		struct Input
		{
			util::geom::Ray	ray;						// through the mouse cursor
			bool			overViewport	{};			// the cursor is over the scene image
			bool			down			{};			// left button held
			bool			pressed			{};			// left button went down this frame
			float			fovY			{ 1.f };	// radians
			float			viewportHeight	{ 720 };	// pixels; with fovY, what one pixel measures at a distance
		};

		/**
		 * What a drag has to respect: the size a resize is clamped to, and
		 * the step positions are rounded to.
		 *
		 */
		struct Limits
		{
			glm::vec3	minSize	{ 3.2f };
			glm::vec3	maxSize	{ 204.8f, 102.4f, 204.8f };
			float		snap	{ 0.f };	// metres; 0 moves freely
		};

		/*!
			@brief
				Picks, starts, continues or ends a drag.
			@param region
				The region to move or resize
			@param input
				This frame's pointer state
			@param limits
				Size limits and snap step a drag respects
			@return
				Whether the region changed this frame
			*/
		bool update(TerrainRegion& region, const Input& input, const Limits& limits);

		/*!
			@brief
				Queues the box and the handles.

				Call once per frame, before the renderer's flush.
			@param overlay
				Renderer the triangles are queued on
			@param region
				The region to draw
			@param input
				This frame's pointer state; its ray origin is the eye
			*/
		void draw(graphics::renderer::OverlayRenderer& overlay,
				  const TerrainRegion& region,
				  const Input& input) const;

		/*!
			@brief
				Lets go of whatever is held, e.g. when the gizmo is hidden mid-drag.
			*/
		void release();

		/*!
			@brief
				Whether a handle is under the cursor or being dragged: the mouse is the gizmo's, not
				the camera's.
			@return
				True while a handle is hovered or held
			*/
		bool	hot() const			{ return hover_.has_value() || drag_.has_value(); }

		/*!
			@brief
				Whether a drag is in progress.
			@return
				True while a handle is held
			*/
		bool	dragging() const	{ return drag_.has_value(); }

		/*!
			@brief
				Checks picking, both kinds of drag, snapping, the size limits and the guard against
				a ray that runs along the drag axis.
			@return
				Empty when everything holds, else what failed
			*/
		static std::string selfCheck();

	private:

		// Move is a face's arrow, Resize its dot.
		enum class Kind
		{
			Move,
			Resize
		};

		// One of the handles: which kind, on which face.
		struct Handle
		{
			Kind	kind	{};
			int		axis	{};		// 0 x, 1 y, 2 z
			int		side	{};		// -1 the min face, +1 the max face

			bool operator==(const Handle&) const = default;
		};

		// A drag in progress.
		struct Drag
		{
			Handle			handle;
			TerrainRegion	start;				// the region when the button went down
			glm::vec3		anchor		{};		// a point on the drag axis
			float			startParam	{};		// where along the axis the cursor grabbed it
			float			lastParam	{};
		};

		/** Where a handle sits, and how big it is drawn, for this region and eye. */
		struct Placement
		{
			glm::vec3	dot			{};		// centre of the resize dot
			glm::vec3	arrowFrom	{};
			glm::vec3	arrowTo		{};
			float		unit		{};		// world length of the gizmo's unit at this face
		};

		/*!
			@brief
				Works out a handle's Placement; the eye is the input ray's origin.
			@param region
				The region the handle belongs to
			@param handle
				Which handle
			@param input
				Pointer state giving the eye, field of view and viewport height
			@return
				Where the handle sits and its world-space unit
			*/
		static Placement place(const TerrainRegion& region, const Handle& handle, const Input& input);

		/*!
			@brief
				Finds the handle the ray hits first, if it hits any.
			@param region
				The region whose handles are tested
			@param input
				Pointer state holding the ray
			@return
				The nearest handle hit, or empty
			*/
		static std::optional<Handle> pick(const TerrainRegion& region, const Input& input);

		std::optional<Handle>	hover_;
		std::optional<Drag>		drag_;
	};
}
