/*************************************************************************************************************
\file       OrientedBoxGizmo.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Implements OrientedBoxGizmo: moves the mouse ray into the box's frame for the region gizmo,
            holds that frame during a drag, drives the rotation rings, and self-checks a turned box.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "OrientedBoxGizmo.hpp"

// Engine
#include "graphics/renderer/overlay_renderer.hpp"

// GLM
#include <glm/gtc/matrix_transform.hpp>

// stds
#include <cmath>


namespace vox::editor
{
	namespace
	{
		/*!
			@brief
				Converts world-space pointer state into the region gizmo's input in a box's frame.
			@param input
				Pointer state in world space
			@param origin
				Origin of the box's frame
			@param rotation
				Rotation of the box's frame
			@return
				The same state with the ray moved into the frame
			*/
		RegionGizmo::Input toLocal(
			const OrientedBoxGizmo::Input& input,
			const glm::vec3& origin,
			const glm::quat& rotation)
		{
			const glm::quat inverse = glm::conjugate(rotation);
			RegionGizmo::Input local;
			local.ray = { inverse * (input.ray.origin_ - origin), inverse * input.ray.direction_ };
			local.overViewport = input.overViewport;
			local.down = input.down;
			local.pressed = input.pressed;
			local.fovY = input.fovY;
			local.viewportHeight = input.viewportHeight;
			return local;
		}
	}

	/*!
		@brief
			The origin of the box's frame, fixed for the length of a drag.
		@param box
			The box
		@return
			The held origin during a drag, else the box's lowest corner
		*/
	glm::vec3 OrientedBoxGizmo::frameOrigin(const OrientedBox& box) const
	{
		// The box's lowest corner, so that with no rotation the frame is the world moved onto
		// the voxel lattice, and the region gizmo's snapping lands on whole voxels.
		return held_ ? heldOrigin_ : box.centre - box.rotation * (0.5f * box.size);
	}

	/*!
		@brief
			Runs the rings first (they own the mouse while dragged), then the region gizmo in the
			box's frame, holding the frame's origin while a handle is dragged.
		@param box
			The box to move, resize or turn
		@param input
			This frame's pointer state, in world space
		@param options
			Limits, snapping and whether the rings are on
		@return
			Whether the box changed
		*/
	bool OrientedBoxGizmo::update(OrientedBox& box, const Input& input, const Options& options)
	{
		const OrientedBox before = box;

		// The rings first: they sit outside the box, and a ring that is being dragged owns the mouse.
		if (options.rings && !region_.dragging())
		{
			RotateRings::Input ring;
			ring.ray = input.ray;
			ring.overViewport = input.overViewport;
			ring.down = input.down;
			ring.pressed = input.pressed && !region_.hot();
			ring.freeAngle = input.freeAngle;
			ring.fovY = input.fovY;
			ring.viewportHeight = input.viewportHeight;
			rings_.update(box.rotation, box.centre, ringRadius(box), ring, options.snapDegrees);
		}
		else
			rings_.release();

		if (!rings_.dragging())
		{
			const glm::vec3 origin = frameOrigin(box);
			const glm::quat inverse = glm::conjugate(box.rotation);
			TerrainRegion local;
			local.size_ = box.size;
			local.min_ = inverse * (box.centre - origin) - 0.5f * box.size;

			RegionGizmo::Input in = toLocal(input, origin, box.rotation);
			if (region_.update(local, in, options.limits))
			{
				box.size = local.size_;
				box.centre = origin + box.rotation * local.centre();
			}
			// Started just now: hold the frame where it is until the button comes up.
			if (region_.dragging() && !held_)
			{
				held_ = true;
				heldOrigin_ = origin;
			}
			if (!region_.dragging())
				held_ = false;
		}
		return !(box == before);
	}

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
	void OrientedBoxGizmo::draw(
		graphics::renderer::OverlayRenderer& overlay,
		const OrientedBox& box,
		const Input& input,
		const Options& options) const
	{
		const glm::vec3 origin = frameOrigin(box);
		const glm::quat inverse = glm::conjugate(box.rotation);
		TerrainRegion local;
		local.size_ = box.size;
		local.min_ = inverse * (box.centre - origin) - 0.5f * box.size;

		overlay.setTransform(glm::translate(glm::mat4{ 1.f }, origin) * glm::mat4_cast(box.rotation));
		region_.draw(overlay, local, toLocal(input, origin, box.rotation));
		overlay.resetTransform();

		if (options.rings)
		{
			RotateRings::Input ring;
			ring.ray = input.ray;
			ring.fovY = input.fovY;
			ring.viewportHeight = input.viewportHeight;
			rings_.draw(overlay, box.rotation, box.centre, ringRadius(box), ring);
		}
	}

	/*!
		@brief
			Runs RotateRings' check, then drags the resize dot of a box turned a quarter about y and
			checks it grows along its own axis with the far face fixed.
		@return
			Empty when every check passes, else what failed
		*/
	std::string OrientedBoxGizmo::selfCheck()
	{
		if (std::string fault = RotateRings::selfCheck(); !fault.empty())
			return fault;

		const auto nearly = [](const glm::vec3& a, const glm::vec3& b)
		{
			return glm::length(a - b) < 2e-2f;
		};
		Options options;
		options.rings = false;
		options.limits.minSize = glm::vec3{ 1.f };
		options.limits.maxSize = glm::vec3{ 40.f };
		options.limits.snap = 0.f;

		// --------------------------- Set up a turned box ---------------------------
		// A box turned a quarter about y: its own +x now points along world -z. Dragging the +x
		// resize dot must grow the box along that direction and leave the far face where it was.
		OrientedBox box;
		box.centre = { 0.f, 0.f, 0.f };
		box.size = { 4.f, 2.f, 2.f };
		box.rotation = glm::angleAxis(glm::radians(90.f), glm::vec3{ 0.f, 1.f, 0.f });
		const glm::vec3 farFace = box.centre + box.rotation * glm::vec3{ -2.f, 0.f, 0.f };

		// Looking straight down from above, where that dot is in plain view.
		const auto inputAt = [](const glm::vec3& target, bool down, bool pressed)
		{
			Input in;
			in.ray = { { target.x, 60.f, target.z }, { 0.f, -1.f, 0.f } };
			in.overViewport = true;
			in.down = down;
			in.pressed = pressed;
			return in;
		};

		// --------------------------- Find and drag the +x dot ---------------------------
		// Find the dot by sweeping outward from the +x face along the box's x axis.
		OrientedBoxGizmo gizmo;
		const glm::vec3 outward = box.rotation * glm::vec3{ 1.f, 0.f, 0.f };
		glm::vec3 grab{ 0.f };
		bool found = false;
		for (float d = 2.f; d < 6.f && !found; d += 0.02f)
		{
			gizmo.update(box, inputAt(outward * d, false, false), options);
			if (gizmo.hot())
			{
				grab = outward * d;
				found = true;
			}
		}
		if (!found)
			return "a turned box's handles are found along its own axes";

		gizmo.update(box, inputAt(grab, true, true), options);
		if (!gizmo.dragging())
			return "a press on a turned box's handle starts a drag";
		gizmo.update(box, inputAt(grab + outward * 3.f, true, false), options);
		if (std::fabs(box.size.x - 7.f) > 2e-2f)
			return "dragging a turned box's resize dot changes its size along its own axis";
		if (!nearly(box.centre + box.rotation * glm::vec3{ -0.5f * box.size.x, 0.f, 0.f }, farFace))
			return "and the opposite face stays where it was";
		gizmo.update(box, inputAt(grab + outward * 3.f, false, false), options);
		if (gizmo.dragging())
			return "releasing the button ends the drag";

		return {};
	}
}
