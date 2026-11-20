/*************************************************************************************************************
\file       RotateRings.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Implements RotateRings: ring picking, angle measurement in a ring's plane, unwrapped and
            snapped drags, drawing the rings, and the rings' self-check.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "RotateRings.hpp"

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

		constexpr float kPi = 3.14159265358979f;
		constexpr float kRingPixels = 2.4f; // drawn width
		constexpr float kPickPixels = 8.f;  // how far from the ring a click still counts

		const glm::vec3 kAxisColour[3] = { { 0.93f, 0.26f, 0.26f },
			                               { 0.35f, 0.80f, 0.30f },
			                               { 0.27f, 0.50f, 0.96f } };

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
		float worldPerPixel(const glm::vec3& point, const RotateRings::Input& input)
		{
			const float distance = std::max(0.25f, glm::length(point - input.ray.origin_));
			return 2.f * distance * std::tan(0.5f * input.fovY) / std::max(1.f, input.viewportHeight);
		}

		/*!
			@brief
				Builds two unit vectors square to w and each other.

				u x v = w, so angles around w run counter-clockwise.
			@param w
				The unit axis to build the basis around
			@param u
				Receives the first basis vector
			@param v
				Receives the second basis vector
			*/
		void basisAround(const glm::vec3& w, glm::vec3& u, glm::vec3& v)
		{
			const glm::vec3 helper =
				std::fabs(w.y) < 0.9f ? glm::vec3{ 0.f, 1.f, 0.f } : glm::vec3{ 1.f, 0.f, 0.f };
			u = glm::normalize(glm::cross(helper, w));
			v = glm::cross(w, u);
		}
	}

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
	std::optional<float> RotateRings::angleOn(
		const util::geom::Ray& ray,
		const glm::vec3& centre,
		const glm::vec3& normal)
	{
		const std::optional<float> t = util::geom::rayPlane(ray, centre, normal);
		if (!t)
			return std::nullopt;

		glm::vec3 u, v;
		basisAround(normal, u, v);
		const glm::vec3 p = ray.at(*t) - centre;
		return std::atan2(glm::dot(p, v), glm::dot(p, u));
	}

	/*!
		@brief
			Finds the ring whose plane the ray crosses nearest, within kPickPixels of the ring.
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
	std::optional<int> RotateRings::pick(
		const glm::quat& rotation,
		const glm::vec3& centre,
		float radius,
		const Input& input)
	{
		std::optional<int> best;
		float bestT = 1e30f;
		const float slack = kPickPixels * worldPerPixel(centre, input);
		for (int axis = 0; axis < 3; ++axis)
		{
			const std::optional<float> t = util::geom::rayPlane(input.ray, centre, rotation * unitAxis(axis));
			if (!t || *t >= bestT)
				continue;
			if (std::fabs(glm::length(input.ray.at(*t) - centre) - radius) <= slack)
			{
				bestT = *t;
				best = axis;
			}
		}
		return best;
	}

	/*!
		@brief
			Continues or ends a ring drag, or else updates the hover and starts a drag on a press.

			The angle is unwrapped against the last one so a drag past half a turn keeps going, and
			an edge-on ring keeps the last angle.
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
	bool RotateRings::update(
		glm::quat& rotation,
		const glm::vec3& centre,
		float radius,
		const Input& input,
		float snapDegrees)
	{
		if (drag_ && !input.down)
			drag_.reset();
		if (drag_)
		{
			// Seen edge on, the ring's plane has no crossing to measure: keep the last angle.
			if (const std::optional<float> angle = angleOn(input.ray, centre, drag_->normal))
			{
				// Unwrapped against the last one, so a drag past half a turn keeps going.
				float a = *angle;
				while (a - drag_->lastAngle > kPi)
					a -= 2.f * kPi;
				while (a - drag_->lastAngle < -kPi)
					a += 2.f * kPi;
				drag_->lastAngle = a;
			}
			float delta = drag_->lastAngle - drag_->startAngle;
			if (snapDegrees > 0.f && !input.freeAngle)
			{
				const float step = glm::radians(snapDegrees);
				delta = std::round(delta / step) * step;
			}
			const glm::quat before = rotation;
			rotation = glm::normalize(glm::angleAxis(delta, drag_->normal) * drag_->start);
			return std::fabs(glm::dot(before, rotation)) < 1.f - 1e-7f;
		}

		hover_ = input.overViewport ? pick(rotation, centre, radius, input) : std::nullopt;
		if (input.pressed && hover_)
		{
			const glm::vec3 normal = rotation * unitAxis(*hover_);
			if (const std::optional<float> angle = angleOn(input.ray, centre, normal))
				drag_ = Drag{ *hover_, normal, rotation, *angle, *angle };
		}
		return false;
	}

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
	void RotateRings::draw(
		OverlayRenderer& overlay,
		const glm::quat& rotation,
		const glm::vec3& centre,
		float radius,
		const Input& input) const
	{
		const float pixel = worldPerPixel(centre, input);
		for (int axis = 0; axis < 3; ++axis)
		{
			const bool held = drag_ && drag_->axis == axis;
			const bool lit = held || (!drag_ && hover_ && *hover_ == axis);
			const glm::vec3 base = kAxisColour[axis];
			const glm::vec4 colour{ held  ? glm::vec3{ 1.f, 0.92f, 0.35f }
				                    : lit ? glm::mix(base, glm::vec3{ 1.f }, 0.45f)
				                          : base,
				                    drag_ && !held ? 0.30f : 0.95f };
			overlay.circle(
				OverlayRenderer::Layer::OnTop,
				centre,
				held ? drag_->normal : rotation * unitAxis(axis),
				radius,
				kRingPixels * (lit ? 1.5f : 1.f) * pixel,
				input.ray.origin_,
				colour);
		}
	}

	/*!
		@brief
			Checks picking, dragging past half a turn, snapping, the free-angle key and that an
			edge-on ring cannot be grabbed, with no window.
		@return
			Empty when every check passes, else what failed
		*/
	std::string RotateRings::selfCheck()
	{
		const glm::vec3 centre{ 0.f };
		constexpr float radius = 2.f;

		const auto nearly = [](const glm::vec3& a, const glm::vec3& b)
		{
			return glm::length(a - b) < 1e-3f;
		};

		// Looking down -z at a point of the z = 0 plane.
		const auto inputAt = [](float x, float y, bool down, bool pressed, bool freeAngle = false)
		{
			Input in;
			in.ray = { { x, y, 10.f }, { 0.f, 0.f, -1.f } };
			in.overViewport = true;
			in.down = down;
			in.pressed = pressed;
			in.freeAngle = freeAngle;
			return in;
		};
		const auto at = [](float degrees)
		{
			return glm::vec2{ radius * std::cos(glm::radians(degrees)),
				              radius * std::sin(glm::radians(degrees)) };
		};

		// --------------------------- Empty space ---------------------------
		// A press off the rings grabs nothing.
		{
			RotateRings rings;
			glm::quat q{ 1.f, 0.f, 0.f, 0.f };
			rings.update(q, centre, radius, inputAt(0.9f, 0.3f, true, true), 15.f);
			if (rings.dragging())
				return "a press away from every ring starts no drag";
		}

		// --------------------------- Drag around the z ring ---------------------------
		// The grabbed point follows the pointer around the ring.
		{
			RotateRings rings;
			glm::quat q{ 1.f, 0.f, 0.f, 0.f };
			rings.update(q, centre, radius, inputAt(at(0).x, at(0).y, false, false), 0.f);
			if (!rings.hot())
				return "a pointer on the z ring lights it";
			rings.update(q, centre, radius, inputAt(at(0).x, at(0).y, true, true), 0.f);
			if (!rings.dragging())
				return "a press on a ring starts a drag";
			if (!rings.update(q, centre, radius, inputAt(at(90).x, at(90).y, true, false), 0.f))
				return "dragging a ring reports a change";
			if (!nearly(q * glm::vec3{ 1, 0, 0 }, { 0, 1, 0 }))
				return "a quarter of the way round the z ring turns x onto y";
			rings.update(q, centre, radius, inputAt(at(200).x, at(200).y, true, false), 0.f);
			if (!nearly(
					q * glm::vec3{ 1, 0, 0 },
					{ std::cos(glm::radians(200.f)), std::sin(glm::radians(200.f)), 0 }))
				return "a drag past half a turn keeps turning the same way";
			rings.update(q, centre, radius, inputAt(at(200).x, at(200).y, false, false), 0.f);
			if (rings.dragging())
				return "releasing the button ends the drag";
		}

		// --------------------------- Snapping ---------------------------
		// Snapping rounds the turn; the free-angle key lifts it.
		{
			RotateRings rings;
			glm::quat q{ 1.f, 0.f, 0.f, 0.f };
			rings.update(q, centre, radius, inputAt(at(0).x, at(0).y, true, true), 15.f);
			rings.update(q, centre, radius, inputAt(at(50).x, at(50).y, true, false), 15.f);
			if (!nearly(
					q * glm::vec3{ 1, 0, 0 },
					{ std::cos(glm::radians(45.f)), std::sin(glm::radians(45.f)), 0 }))
				return "a snapped drag lands on a multiple of the step";
			rings.update(q, centre, radius, inputAt(at(50).x, at(50).y, true, false, true), 15.f);
			if (!nearly(
					q * glm::vec3{ 1, 0, 0 },
					{ std::cos(glm::radians(50.f)), std::sin(glm::radians(50.f)), 0 }))
				return "the free-angle key turns snapping off";
		}

		// --------------------------- Rings follow their box ---------------------------
		// The rings turn with what they turn: after a turn about z, the x ring is where x went.
		{
			RotateRings rings;
			glm::quat q = glm::angleAxis(glm::radians(90.f), glm::vec3{ 0, 0, 1 });
			// The x ring now lies in the plane y = 0... its normal is world y. Seen along -z it is edge on,
			// so nothing is picked there.
			rings.update(q, centre, radius, inputAt(0.f, 1.f, true, true), 0.f);
			if (rings.dragging())
				return "a ring seen exactly edge on cannot be grabbed";
		}

		return {};
	}
}
