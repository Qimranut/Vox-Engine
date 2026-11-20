/*************************************************************************************************************
\file       fly_controller.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      FlyController: merges mouse and stick look, adjusts fly speed from wheel and held input, and
            moves the camera along its own right/forward and world up.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "fly_controller.hpp"

// stds
#include <cmath>


namespace graphics::camera
{
	namespace
	{
		const glm::vec3 kWorldUp{ 0.f, 1.f, 0.f };

		/*!
			@brief
				Wraps an angle into [-pi, pi] so yaw does not grow without bound
				over a long session.
			@param a
				Angle in radians
			@return
				Equivalent angle in [-pi, pi]
			*/
		float wrapAngle(float a)
		{
			constexpr float kTwoPi = 6.2831853f;

			a = std::fmod(a + 3.1415927f, kTwoPi);

			if (a < 0.f)
				a += kTwoPi;

			return a - 3.1415927f;
		}
	}

	/*!
		@brief
			Advances the camera by one frame of input: look, speed change, then movement.
		@param cam
			Camera to drive
		@param in
			This frame's intent
		@param dt
			Real frame time in seconds
		*/
	void FlyController::update(Camera& cam, const ControllerInput& in, float dt)
	{
		// ---------------------------- Look ----------------------------
		//
		// The two unit systems are merged here and nowhere else:
		//   mouse: radians-per-PIXEL   x  pixels-this-frame   -> radians
		//   stick: radians-per-SECOND  x  seconds-this-frame  -> radians
		// Merging them any earlier would mean baking a dt the input system
		// does not own into the mouse path, or dropping it from the stick.

		glm::vec2 lookRad{ 0.f };

		if (in.lookActive_)
			lookRad += in.lookDelta_ * lookSensitivity_;

		lookRad += in.lookStick_ * stickLookSpeed_ * dt;

		if (lookRad.x != 0.f || lookRad.y != 0.f)
		{
			const float sign = invertY_ ? -1.f : 1.f;

			// Both subtract: yaw increases turning LEFT (see camera.hpp) and
			// screen Y grows downwards, so a raw delta has to be negated on
			// both axes to read as "drag the world".
			float yaw = cam.yaw() - lookRad.x;
			float pitch = cam.pitch() - lookRad.y * sign;

			pitch = glm::clamp(pitch, -pitchLimit_, pitchLimit_);

			cam.setYawPitch(wrapAngle(yaw), pitch);
		}

		// --------------------------- Speed ----------------------------

		if (in.scrollDelta_ != 0.f)
		{
			moveSpeed_ = glm::clamp(
				moveSpeed_ * std::pow(scrollStep_, in.scrollDelta_),
				minSpeed_,
				maxSpeed_);
		}

		if (in.zoomRate_ != 0.f)
		{
			// pow(k, rate * dt) compounds to pow(k, rate * T) over T seconds
			// regardless of frame rate - the multiplicative equivalent of
			// scaling by dt, which a wheel tick does not need but a held
			// d-pad does.
			moveSpeed_ = glm::clamp(
				moveSpeed_ * std::pow(stickSpeedStep_, in.zoomRate_ * dt),
				minSpeed_,
				maxSpeed_);
		}

		// --------------------------- Move -----------------------------

		glm::vec3 dir =
			cam.right()		* in.moveAxis_.x +
			kWorldUp		* in.moveAxis_.y +
			cam.forward()	* in.moveAxis_.z;

		const float len2 = glm::dot(dir, dir);

		// Normalize only when actually moving, otherwise diagonals travel
		// 1.41x faster than the cardinals.
		if (len2 > 0.f)
		{
			dir /= std::sqrt(len2);

			const float speed = moveSpeed_ * (in.sprint_ ? sprintMultiplier_ : 1.f);

			cam.setPosition(cam.position() + dir * speed * dt);
		}
	}
}
