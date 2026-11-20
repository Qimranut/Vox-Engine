/*************************************************************************************************************
\file       fly_controller.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      FlyController, the free-flying debug / editor camera controller, with its speed and look
            sensitivity settings.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Camera
#include "../camera.hpp"
#include "controller_input.hpp"


namespace graphics::camera
{
	/**
	 * Free-flying debug / editor camera.
	 *
	 * WASD moves along the camera's own forward and right, Q/E along WORLD
	 * up - camera-relative vertical makes you drift sideways when pitched,
	 * which is disorienting while inspecting a scene.
	 *
	 */
	class FlyController
	{
	public:

		/*!
			@brief
				Advances the camera by one frame of input.
			@param cam
				Camera to drive
			@param in
				This frame's intent
			@param dt
				Real frame time in seconds
			*/
		void update(Camera& cam, const ControllerInput& in, float dt);

		/*!
			@brief
				Sets the base movement speed.
			@param speed
				World units per second
			*/
		void setMoveSpeed(float speed)			{ moveSpeed_ = speed; }

		/*!
			@brief
				Sets the mouse look sensitivity.
			@param radPerPx
				Radians per pixel of mouse movement
			*/
		void setLookSensitivity(float radPerPx)	{ lookSensitivity_ = radPerPx; }

		/*!
			@brief
				Sets the stick look turn rate at full deflection.
			@param radPerSec
				Radians per second
			*/
		void setStickLookSpeed(float radPerSec)	{ stickLookSpeed_ = radPerSec; }

		/*!
			@brief
				Sets whether vertical look is inverted.
			@param invert
				true to invert the Y axis
			*/
		void setInvertY(bool invert)			{ invertY_ = invert; }

		/*!
			@brief
				Current base movement speed.
			@return
				World units per second
			*/
		float moveSpeed() const					{ return moveSpeed_; }

		/*!
			@brief
				Current mouse look sensitivity.
			@return
				Radians per pixel
			*/
		float lookSensitivity() const			{ return lookSensitivity_; }

		/*!
			@brief
				Current stick look turn rate.
			@return
				Radians per second
			*/
		float stickLookSpeed() const			{ return stickLookSpeed_; }

	private:

		float	moveSpeed_			{ 8.f };		// world units / second
		float	sprintMultiplier_	{ 4.f };
		float	lookSensitivity_	{ 0.0022f };	// radians per PIXEL
		float	stickLookSpeed_		{ 2.6f };		// radians per SECOND, ~150 deg/s
		float	scrollStep_			{ 1.15f };		// multiplicative per wheel tick
		float	stickSpeedStep_		{ 6.f };		// speed multiplier per second held
		float	minSpeed_			{ 0.25f };
		float	maxSpeed_			{ 200.f };
		float	pitchLimit_			{ 1.5533431f };	// 89 degrees
		bool	invertY_			{ false };
	};
}
