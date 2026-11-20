/*************************************************************************************************************
\file       RegionGizmo.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Implements RegionGizmo: analytic picking of the face arrows and dots, move and resize drags
            with snapping and size limits, drawing the box and handles, and the gizmo self-check.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "RegionGizmo.hpp"

// Engine
#include "graphics/renderer/overlay_renderer.hpp"

// stds
#include <algorithm>
#include <cmath>


namespace vox::editor
{
	namespace
	{
		using graphics::renderer::OverlayRenderer;
		using util::geom::Ray;

		// Sizes in gizmo units; one unit is this many pixels on screen, wherever the handle is.
		constexpr float kUnitPixels = 13.f;
		constexpr float kDotOffset = 1.7f, kDotRadius = 0.68f;
		constexpr float kArrowFrom = 3.1f, kArrowNeck = 6.6f, kArrowTo = 8.4f;
		constexpr float kShaftRadius = 0.2f, kHeadRadius = 0.68f;
		constexpr float kPickSlack = 1.3f; // a handle is a little easier to hit than it looks
		constexpr float kEdgePixels = 1.6f;
		constexpr int kEdgePieces = 24;

		const glm::vec3 kAxisColour[3] = { { 0.87f, 0.22f, 0.22f },
			                               { 0.22f, 0.78f, 0.26f },
			                               { 0.27f, 0.33f, 0.88f } };
		const glm::vec4 kBoxFill{ 0.80f, 0.86f, 0.96f, 0.11f };
		const glm::vec4 kBoxEdge{ 1.f, 1.f, 1.f, 0.55f };

		/*!
			@brief
				Builds the unit vector along one axis.
			@param axis
				0 x, 1 y, 2 z
			@return
				The unit vector
			*/
		glm::vec3 unitAxis(int axis)
		{
			glm::vec3 v{ 0.f };
			v[axis] = 1.f;
			return v;
		}

		/*!
			@brief
				World length one pixel covers at a point, for a perspective view.
			@param point
				The point in world space
			@param input
				Pointer state giving the eye, field of view and viewport height
			@return
				Metres per pixel at that point
			*/
		float worldPerPixel(const glm::vec3& point, const RegionGizmo::Input& input)
		{
			const float distance = std::max(0.25f, glm::length(point - input.ray.origin_));
			return 2.f * distance * std::tan(0.5f * input.fovY) / std::max(1.f, input.viewportHeight);
		}

		/*!
			@brief
				Rounds a value to the nearest multiple of a step.
			@param value
				The value to round
			@param step
				The step; 0 or less leaves the value as is
			@return
				The rounded value
			*/
		float snapped(float value, float step)
		{
			return step > 0.f ? std::round(value / step) * step : value;
		}
	}

	/*!
		@brief
			Works out where a handle sits on its face, sized to keep one size on screen.
		@param region
			The region the handle belongs to
		@param handle
			Which handle
		@param input
			Pointer state giving the eye, field of view and viewport height
		@return
			The dot centre, arrow ends and world-space unit
		*/
	RegionGizmo::Placement RegionGizmo::place(
		const TerrainRegion& region,
		const Handle& handle,
		const Input& input)
	{
		const glm::vec3 outward = unitAxis(handle.axis) * static_cast<float>(handle.side);
		const glm::vec3 face = region.centre() + outward * (0.5f * region.size_[handle.axis]);
		Placement p;
		p.unit = kUnitPixels * worldPerPixel(face, input);
		p.dot = face + outward * (kDotOffset * p.unit);
		p.arrowFrom = face + outward * (kArrowFrom * p.unit);
		p.arrowTo = face + outward * (kArrowTo * p.unit);
		return p;
	}

	/*!
		@brief
			Finds the handle the ray hits first: dots as spheres, arrows as segments, each a little
			larger than drawn.
		@param region
			The region whose handles are tested
		@param input
			Pointer state holding the ray
		@return
			The nearest handle hit, or empty
		*/
	std::optional<RegionGizmo::Handle> RegionGizmo::pick(const TerrainRegion& region, const Input& input)
	{
		std::optional<Handle> best;
		float bestT = 1e30f;
		for (int axis = 0; axis < 3; ++axis)
			for (int side = -1; side <= 1; side += 2)
			{
				const Placement p = place(region, { Kind::Resize, axis, side }, input);
				if (const auto t = util::geom::raySphere(input.ray, p.dot, kDotRadius * kPickSlack * p.unit);
				    t && *t < bestT)
				{
					bestT = *t;
					best = Handle{ Kind::Resize, axis, side };
				}
				const auto approach = util::geom::closestToSegment(input.ray, p.arrowFrom, p.arrowTo);
				if (approach.distance_ <= kHeadRadius * p.unit && approach.rayT_ < bestT)
				{
					bestT = approach.rayT_;
					best = Handle{ Kind::Move, axis, side };
				}
			}
		return best;
	}

	/*!
		@brief
			Lets go of whatever is held or hovered.
		*/
	void RegionGizmo::release()
	{
		drag_.reset();
		hover_.reset();
	}

	/*!
		@brief
			Continues or ends a drag, or else updates the hover and starts a drag on a press.

			Movement is measured along the handle's axis from where it was grabbed; a ray that runs
			along the axis keeps the last known position.
		@param region
			The region to move or resize
		@param input
			This frame's pointer state
		@param limits
			Size limits and snap step a drag respects
		@return
			Whether the region changed this frame
		*/
	bool RegionGizmo::update(TerrainRegion& region, const Input& input, const Limits& limits)
	{
		if (drag_ && !input.down)
			drag_.reset();
		if (drag_)
		{
			const int a = drag_->handle.axis;
			// A ray that runs along the axis says nothing about where on it the cursor is.
			if (const auto param = util::geom::axisParameter(input.ray, drag_->anchor, unitAxis(a)))
				drag_->lastParam = *param;
			const float delta = drag_->lastParam - drag_->startParam;
			const TerrainRegion& from = drag_->start;
			const TerrainRegion before = region;

			if (drag_->handle.kind == Kind::Move)
			{
				region.min_[a] = snapped(from.min_[a] + delta, limits.snap);
			}
			else if (drag_->handle.side > 0)
			{
				// The min face stays; the max face follows the cursor.
				const float wanted = snapped(from.max()[a] + delta, limits.snap) - from.min_[a];
				region.size_[a] = std::clamp(wanted, limits.minSize[a], limits.maxSize[a]);
			}
			else
			{
				const float wanted = from.max()[a] - snapped(from.min_[a] + delta, limits.snap);
				region.size_[a] = std::clamp(wanted, limits.minSize[a], limits.maxSize[a]);
				region.min_[a] = from.max()[a] - region.size_[a];
			}
			return !(region == before);
		}

		hover_ = input.overViewport ? pick(region, input) : std::nullopt;
		if (input.pressed && hover_)
		{
			const Placement p = place(region, *hover_, input);
			const glm::vec3 anchor = hover_->kind == Kind::Resize ? p.dot : 0.5f * (p.arrowFrom + p.arrowTo);
			if (const auto param = util::geom::axisParameter(input.ray, anchor, unitAxis(hover_->axis)))
				drag_ = Drag{ *hover_, region, anchor, *param, *param };
		}
		return false;
	}

	/*!
		@brief
			Queues the translucent box and its edges on the Scene layer, and the handles on OnTop,
			lit when hovered or held.
		@param overlay
			Renderer the triangles are queued on
		@param region
			The region to draw
		@param input
			This frame's pointer state; its ray origin is the eye
		*/
	void RegionGizmo::draw(OverlayRenderer& overlay, const TerrainRegion& region, const Input& input) const
	{
		using Layer = OverlayRenderer::Layer;
		const glm::vec3 eye = input.ray.origin_;
		const glm::vec3 mn = region.min_, mx = region.max();

		// The volume, hidden by terrain in front of it like anything else in the scene.
		overlay.box(Layer::Scene, mn, mx, kBoxFill);
		const auto corner = [&](int i)
		{
			return glm::vec3{ (i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z };
		};
		for (int i = 0; i < 8; ++i)
			for (int bit = 1; bit <= 4; bit <<= 1)
				if ((i & bit) == 0)
				{
					// In pieces: one strip sized for its midpoint would be a beam at the near end of a long
					// edge.
					const glm::vec3 a = corner(i), b = corner(i | bit);
					for (int piece = 0; piece < kEdgePieces; ++piece)
					{
						const glm::vec3 from = glm::mix(a, b, static_cast<float>(piece) / kEdgePieces),
										to = glm::mix(a, b, static_cast<float>(piece + 1) / kEdgePieces);
						overlay.edge(
							Layer::Scene,
							from,
							to,
							kEdgePixels * worldPerPixel(from, input),
							kEdgePixels * worldPerPixel(to, input),
							eye,
							kBoxEdge);
					}
				}

		// The handles, never hidden.
		for (int axis = 0; axis < 3; ++axis)
			for (int side = -1; side <= 1; side += 2)
				for (const Kind kind : { Kind::Resize, Kind::Move })
				{
					const Handle handle{ kind, axis, side };
					const bool held = drag_ && drag_->handle == handle;
					const bool lit = held || (!drag_ && hover_ && *hover_ == handle);
					// While one handle is held the others step back.
					const float alpha = drag_ && !held ? 0.30f : 1.f;
					const glm::vec3 base = kAxisColour[axis];
					const glm::vec4 colour{ held  ? glm::vec3{ 1.f, 0.92f, 0.35f }
						                    : lit ? glm::mix(base, glm::vec3{ 1.f }, 0.45f)
						                          : base,
						                    alpha };
					const float grow = lit ? 1.18f : 1.f;

					const Placement p = place(region, handle, input);
					if (kind == Kind::Resize)
					{
						overlay.sphere(Layer::OnTop, p.dot, kDotRadius * grow * p.unit, colour);
					}
					else
					{
						const glm::vec3 outward = unitAxis(axis) * static_cast<float>(side);
						const glm::vec3 neck = p.arrowFrom + outward * ((kArrowNeck - kArrowFrom) * p.unit);
						overlay
							.cylinder(Layer::OnTop, p.arrowFrom, neck, kShaftRadius * grow * p.unit, colour);
						overlay.cone(Layer::OnTop, neck, p.arrowTo, kHeadRadius * grow * p.unit, colour);
					}
				}
	}

	/*!
		@brief
			Checks picking, both kinds of drag, snapping, the size limits and the guard against a
			ray that runs along the drag axis, with no window.
		@return
			Empty when everything holds, else what failed
		*/
	std::string RegionGizmo::selfCheck()
	{
		// A camera 40 m south of a 10 x 4 x 10 box, level with its middle, looking north (-z).
		const TerrainRegion original{ { 0.f, 0.f, 0.f }, { 10.f, 4.f, 10.f } };
		const glm::vec3 eye{ 5.f, 2.f, 50.f };
		Limits limits;
		limits.minSize = glm::vec3{ 2.f };
		limits.maxSize = glm::vec3{ 30.f };
		limits.snap = 0.5f;

		const auto inputAt = [&](const glm::vec3& target, bool down, bool pressed)
		{
			Input in;
			in.ray = { eye, glm::normalize(target - eye) };
			in.overViewport = true;
			in.down = down;
			in.pressed = pressed;
			in.fovY = 1.0472f;
			in.viewportHeight = 800.f;
			return in;
		};
		const auto nearly = [](float a, float b)
		{
			return std::fabs(a - b) < 1e-3f;
		};

		// --------------------------- Empty space ---------------------------
		// Nothing under the cursor: no hot handle, and a press starts nothing.
		{
			RegionGizmo g;
			TerrainRegion r = original;
			g.update(r, inputAt({ 5.f, 30.f, 5.f }, true, true), limits);
			if (g.hot() || !(r == original))
				return "a press on empty space grabbed a handle";
		}

		// --------------------------- Resize: +x dot ---------------------------
		// The +x dot resizes: drag it 3.2 m outward, snapped to 0.5 -> 13 m wide, min face unmoved.
		{
			RegionGizmo g;
			TerrainRegion r = original;
			const glm::vec3 dot = place(r, { Kind::Resize, 0, +1 }, inputAt({}, false, false)).dot;
			g.update(r, inputAt(dot, false, false), limits);
			if (!g.hot())
				return "the resize dot was not picked";
			g.update(r, inputAt(dot, true, true), limits);
			if (!g.dragging())
				return "pressing the resize dot did not start a drag";
			g.update(r, inputAt(dot + glm::vec3{ 3.2f, 0.f, 0.f }, true, false), limits);
			if (!nearly(r.size_.x, 13.f) || !nearly(r.min_.x, 0.f))
				return "dragging the +x dot did not resize from the max face";
			if (!nearly(r.size_.y, 4.f) || !nearly(r.size_.z, 10.f))
				return "a resize changed another axis";
			// Far past the limit: clamped, and still anchored on the min face.
			g.update(r, inputAt(dot + glm::vec3{ 200.f, 0.f, 0.f }, true, false), limits);
			if (!nearly(r.size_.x, 30.f) || !nearly(r.min_.x, 0.f))
				return "a resize ignored the maximum size";
			g.update(r, inputAt(dot, false, false), limits);
			if (g.dragging())
				return "releasing the button did not end the drag";
		}

		// --------------------------- Resize: -x dot ---------------------------
		// The -x dot moves the min face and keeps the max face at 10.
		{
			RegionGizmo g;
			TerrainRegion r = original;
			const glm::vec3 dot = place(r, { Kind::Resize, 0, -1 }, inputAt({}, false, false)).dot;
			g.update(r, inputAt(dot, false, false), limits);
			g.update(r, inputAt(dot, true, true), limits);
			g.update(r, inputAt(dot + glm::vec3{ -4.f, 0.f, 0.f }, true, false), limits);
			if (!nearly(r.min_.x, -4.f) || !nearly(r.max().x, 10.f))
				return "dragging the -x dot did not keep the max face in place";
			g.update(r, inputAt(dot + glm::vec3{ 100.f, 0.f, 0.f }, true, false), limits);
			if (!nearly(r.size_.x, 2.f) || !nearly(r.max().x, 10.f))
				return "a resize ignored the minimum size";
		}

		// --------------------------- Move: +y arrow ---------------------------
		// The +y arrow moves the whole box and keeps its size.
		{
			RegionGizmo g;
			TerrainRegion r = original;
			const Placement p = place(r, { Kind::Move, 1, +1 }, inputAt({}, false, false));
			const glm::vec3 grab = 0.5f * (p.arrowFrom + p.arrowTo);
			g.update(r, inputAt(grab, false, false), limits);
			if (!g.hot())
				return "the move arrow was not picked";
			g.update(r, inputAt(grab, true, true), limits);
			g.update(r, inputAt(grab + glm::vec3{ 0.f, 2.7f, 0.f }, true, false), limits);
			if (!nearly(r.min_.y, 2.5f) || !(r.size_ == original.size_))
				return "dragging the +y arrow did not move the box by a snapped step";
			if (!nearly(r.min_.x, 0.f) || !nearly(r.min_.z, 0.f))
				return "a move drifted off its axis";
		}

		// --------------------------- Ray along the drag axis ---------------------------
		// Looking straight down the drag axis: the position along it is unknowable, so nothing moves.
		{
			RegionGizmo g;
			TerrainRegion r = original;
			const glm::vec3 dot = place(r, { Kind::Resize, 2, +1 }, inputAt({}, false, false)).dot;
			Input in = inputAt(dot, true, true);
			in.ray = { { dot.x, dot.y, 60.f }, { 0.f, 0.f, -1.f } };
			g.update(r, inputAt(dot, false, false), limits);
			g.update(r, in, limits);
			if (g.dragging() || !(r == original))
				return "a drag started along a ray parallel to its axis";
		}

		return {};
	}
}
