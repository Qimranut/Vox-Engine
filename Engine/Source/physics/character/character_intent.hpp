/*************************************************************************************************************
\file       character_intent.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      physics::character::Intent: what the player wants the character to do (move, sprint, buffered
            jump, heading), resolved from input by the engine's bus subscribers.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// GLM
#include <glm/glm.hpp>


namespace physics::character
{
	// How long a jump press waits for the ground before it is dropped.
	inline constexpr float kJumpBufferSeconds = 0.1f;

	/**
	 * What the player wants the character to do, already resolved from input.
	 *
	 * The physics counterpart of graphics::camera::ControllerInput. Filled by
	 * the engine's bus subscribers (subscribeCharacterIntent in
	 * engine/wiring/event_wiring.cpp),
	 * so physics never sees a key or a pad.
	 *
	 * Consumed inside the fixed timestep, which runs 0..N times per frame.
	 * move_ and sprint_ are HELD state, so re-reading them every substep is
	 * correct. A jump is an EDGE: the handler sets jumpPending_ and the first
	 * substep that can act on it clears it.
	 *
	 */
	struct Intent
	{
		// x = camera right, y = camera forward (flattened). Magnitude kept, so
		// a half-pushed stick walks; clamped to 1 by the controller.
		glm::vec2	move_			{ 0.f };

		bool		sprint_			{ false };

		// Set by the Jump handler together with jumpBuffer_ (seconds). The
		// request survives that long, so a press slightly before landing
		// still jumps.
		bool		jumpPending_	{ false };
		float		jumpBuffer_		{ 0.f };

		// Heading movement is relative to - the camera's yaw, radians.
		float		headingYaw_		{ 0.f };
	};
}
