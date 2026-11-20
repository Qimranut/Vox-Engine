/*************************************************************************************************************
\file       controller_input.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      ControllerInput, one frame of device-independent camera intent consumed by the camera
            controllers, and damp(), the frame-rate independent smoothing factor they share.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// GLM
#include <glm/glm.hpp>


namespace graphics::camera
{
	/**
	 * One frame of camera intent, already resolved from whatever device
	 * produced it.
	 *
	 * The controllers take this rather than input::System on purpose:
	 *  - graphics never depends on the input module's semantics, only on
	 *    this struct,
	 *  - the input wiring lives in exactly one place (the event subscribers
	 *    that fill this in, subscribeCameraIntent() in engine/wiring/event_wiring.cpp),
	 *  - controllers can be exercised with a synthetic struct.
	 *
	 */
	struct ControllerInput
	{
		// ----------------------------- Look -----------------------------
		//
		// Two channels because the units differ and cannot be added. A mouse
		// delta is frame-absolute; a stick is a rate. Multiplying the mouse by
		// dt makes the same flick turn further at 30 fps than at 144, and NOT
		// multiplying the stick by dt makes turn speed proportional to frame
		// rate. Merging them is the controller's job, after it knows dt.

		/**
		 * Pointer movement this frame, in PIXELS. Frame-absolute: it already
		 * IS the distance moved, so it must NOT be scaled by dt. Scaled by a
		 * radians-per-PIXEL sensitivity.
		 * +x is the mouse moving right, +y is the mouse moving DOWN.
		 */
		glm::vec2	lookDelta_		{ 0.f };

		/**
		 * Stick look intent, each component in [-1, 1], post-deadzone. A
		 * RATE: it MUST be scaled by dt and by a radians-per-SECOND speed.
		 * Sign convention matched to lookDelta_, so +y means "look down".
		 */
		glm::vec2	lookStick_		{ 0.f };

		// ----------------------------- Move -----------------------------

		// Movement intent. x = camera right, y = world up, z = camera forward.
		// NOT normalized: a keyboard diagonal has length 1.41 and a
		// half-pushed stick has length 0.5. The controller decides.
		glm::vec3	moveAxis_		{ 0.f };

		// ------------------------- Zoom / speed -------------------------

		/** Wheel ticks this frame. Frame-absolute and discrete: do NOT scale by dt. */
		float		scrollDelta_	{ 0.f };

		/** Continuous zoom or speed intent in [-1, 1]. A RATE: scale by dt. */
		float		zoomRate_		{ 0.f };

		// ----------------------------- Flags ----------------------------

		// Sprint modifier held.
		bool		sprint_			{ false };

		/**
		 * Gates lookDelta_ ONLY. Stick look is always live: a gamepad has no
		 * OS cursor to fight with, and requiring a hold-to-look button on a
		 * pad is wrong on every console game ever shipped.
		 */
		bool		lookActive_		{ false };
	};

	/*!
		@brief
			Frame-rate independent smoothing factor.

			Returns the blend weight that moves a value halfway to its target
			every halfLife seconds, regardless of frame rate. A fixed per-frame
			weight - mix(a, b, 0.1f) - behaves completely differently at 60 and
			144 fps and is the most common camera-smoothing bug there is.
		@param halfLife
			Seconds to cover half the remaining distance
		@param dt
			Frame time in seconds
		@return
			Blend weight in [0, 1]
		*/
	float damp(float halfLife, float dt);
}
