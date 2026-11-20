/*************************************************************************************************************
\file       graphics_events.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Bus events published about graphics: CameraUpdated, the camera's pose once it has moved for
            the frame.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// GLM
#include <glm/glm.hpp>


namespace graphics::events
{
	/**
	 * The camera finished moving for this frame.
	 *
	 * Published by the engine on the main thread straight after
	 * graphics::System::update, not by graphics itself - graphics is meant to
	 * move onto a scheduler worker one day, and the bus is main-thread only.
	 *
	 * Consumers that steer relative to the view (the player character) read
	 * yaw from here; they run next frame, which is the normal one-frame
	 * latency for camera-relative movement.
	 *
	 */
	struct CameraUpdated
	{
		glm::vec3	position_	{ 0.f };
		glm::vec3	forward_	{ 0.f, 0.f, -1.f };
		float		yaw_		{ 0.f };	// radians, see camera.hpp
		float		pitch_		{ 0.f };	// radians
	};
}
