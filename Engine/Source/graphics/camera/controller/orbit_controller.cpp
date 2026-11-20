/*************************************************************************************************************
\file       orbit_controller.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      OrbitController: unsmoothed look, damped zoom and pivot follow, and the spring arm that pulls
            the eye in when the collision probe hits something. Also defines damp().

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "orbit_controller.hpp"

// stds
#include <algorithm>
#include <cmath>


namespace graphics::camera
{
	namespace
	{
		const glm::vec3 kWorldUp{ 0.f, 1.f, 0.f };
	}

	/*!
		@brief
			Frame-rate independent smoothing factor: the weight that halves the
			remaining distance every halfLife seconds.
		@param halfLife
			Seconds to cover half the remaining distance; 0 or less snaps
		@param dt
			Frame time in seconds
		@return
			Blend weight in [0, 1]
		*/
	float damp(float halfLife, float dt)
	{
		if (halfLife <= 0.f)
			return 1.f;

		return 1.f - std::exp2(-dt / halfLife);
	}

	/*!
		@brief
			Drops all smoothing so the camera is exactly where it belongs on the next frame.

			Resets the pivot and distance, and zeroes the arm so the next update
			takes whatever length the probe allows straight away.
		*/
	void OrbitController::snapToTarget()
	{
		smoothedPivot_ = target_ + pivotOffset_;
		distance_ = glm::clamp(desiredDistance_, minDistance_, maxDistance_);

		// Equal, so the next update treats the arm as not compressed and takes
		// whatever length the probe allows straight away.
		armLength_ = 0.f;
		lastIdealArm_ = 0.f;

		initialized_ = true;
	}

	/*!
		@brief
			Advances the camera by one frame of input: look, zoom, damped follow, then the spring arm.
		@param cam
			Camera to drive
		@param in
			This frame's intent
		@param dt
			Real frame time in seconds
		*/
	void OrbitController::update(Camera& cam, const ControllerInput& in, float dt)
	{
		// ---------------------------- Look ----------------------------
		// Unsmoothed on purpose - mouse look must be 1:1. The stick path adds
		// a rate, not a filter, so that stays true.
		//
		//   mouse: radians-per-PIXEL   x  pixels-this-frame   -> radians
		//   stick: radians-per-SECOND  x  seconds-this-frame  -> radians

		glm::vec2 lookRad{ 0.f };

		if (in.lookActive_)
			lookRad += in.lookDelta_ * lookSensitivity_;

		lookRad += in.lookStick_ * stickLookSpeed_ * dt;

		if (lookRad.x != 0.f || lookRad.y != 0.f)
		{
			const float sign = invertY_ ? -1.f : 1.f;

			yaw_ -= lookRad.x;
			pitch_ -= lookRad.y * sign;

			pitch_ = glm::clamp(pitch_, pitchMin_, pitchMax_);
		}

		// --------------------------- Zoom -----------------------------

		const float zoom =
			in.scrollDelta_ * scrollStep_				// ticks  * units/tick
			+ in.zoomRate_ * stickZoomSpeed_ * dt;		// [-1,1] * units/s * s

		if (zoom != 0.f)
		{
			desiredDistance_ = glm::clamp(
				desiredDistance_ - zoom,
				minDistance_,
				maxDistance_);
		}

		// -------------------------- Follow ----------------------------

		const glm::vec3 pivot = target_ + pivotOffset_;

		if (!initialized_)
			snapToTarget();

		const float pivotWeight = damp(pivotHalfLife_, dt);
		const float distWeight = damp(distanceHalfLife_, dt);

		smoothedPivot_ = glm::mix(smoothedPivot_, pivot, pivotWeight);

		distance_ = glm::mix(
			distance_,
			glm::clamp(desiredDistance_, minDistance_, maxDistance_),
			distWeight);

		// Orientation first: the eye offset is expressed in the camera's own
		// basis, so the camera has to be oriented before it is positioned.
		cam.setYawPitch(yaw_, pitch_);

		const glm::vec3 idealEye =
			smoothedPivot_
			- cam.forward() * distance_
			+ cam.right() * shoulderOffset_.x
			+ kWorldUp * shoulderOffset_.y;

		// ------------------------- Spring arm -------------------------
		//
		// Sweep a sphere from the pivot to where the eye wants to be, shoulder
		// offset included - otherwise the offset alone can push the eye
		// through a wall the player is hugging.

		const glm::vec3 toEye = idealEye - smoothedPivot_;
		const float idealArm = glm::length(toEye);

		if (!probe_ || idealArm < 1e-4f)
		{
			armLength_ = idealArm;
			lastIdealArm_ = idealArm;
			cam.setPosition(idealEye);
			return;
		}

		const glm::vec3 dir = toEye / idealArm;
		const float allowed = glm::clamp(probe_(smoothedPivot_, dir, idealArm, probeRadius_), 0.f, idealArm);

		// Compressed = shorter than the arm wanted LAST frame. Comparing with
		// this frame's ideal would call a zoom-out "compressed" and ease it a
		// second time on top of distance_'s own smoothing.
		const bool compressed = armLength_ < lastIdealArm_ - 1e-3f;

		if (!compressed || allowed <= armLength_)
		{
			// Follow exactly, and snap in the moment something is in the way.
			armLength_ = allowed;
		}
		else
		{
			// Pulled in earlier and the way is clearing: ease back out.
			armLength_ = std::min(allowed,
				glm::mix(armLength_, allowed, damp(armRecoverHalfLife_, dt)));
		}

		lastIdealArm_ = idealArm;

		cam.setPosition(smoothedPivot_ + dir * armLength_);
	}
}
