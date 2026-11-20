/*************************************************************************************************************
\file       orbit_controller.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      OrbitController, the close third-person over-the-shoulder camera with damped follow and a
            collision-probed spring arm.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Camera
#include "../camera.hpp"
#include "controller_input.hpp"

// stds
#include <functional>


namespace graphics::camera
{
	/**
	 * Close third-person, over-the-shoulder camera.
	 *
	 * Orbits a target point at a damped distance, offset to one side so the
	 * subject does not sit dead centre. Until there is a player entity, the
	 * target is whatever setTarget() was last given.
	 *
	 * The pivot and the distance are smoothed; yaw and pitch deliberately
	 * are NOT - mouse look has to be 1:1 or it reads as input lag.
	 *
	 */
	class OrbitController
	{
	public:

		/**
		 * Sweeps a sphere and reports how far it can travel.
		 *
		 * glm-only on purpose: graphics does not know physics exists. The
		 * engine supplies an implementation backed by physics::System.
		 *
		 * [param] origin
		 * Sweep start, world space
		 *
		 * [param] direction
		 * Unit sweep direction
		 *
		 * [param] maxDistance
		 * Sweep length
		 *
		 * [param] radius
		 * Sphere radius
		 *
		 * [return]
		 * Distance to the first obstruction, or maxDistance when clear
		 *
		 */
		using CollisionProbe = std::function<float(const glm::vec3& origin,
												   const glm::vec3& direction,
												   float maxDistance,
												   float radius)>;

		/*!
			@brief
				Installs the spring arm's collision query. Without one the camera
				passes through walls.
			@param probe
				Query to use, or an empty function to disable collision
			*/
		void setCollisionProbe(CollisionProbe probe)	{ probe_ = std::move(probe); }

		/*!
			@brief
				Sets the radius the arm keeps between the eye and geometry.

				Should cover the near plane's corners, or the view clips into
				walls at an angle.
			@param radius
				Probe sphere radius
			*/
		void setProbeRadius(float radius)			{ probeRadius_ = radius; }

		/*!
			@brief
				Current arm length after collision, pivot to eye.
			@return
				Arm length in world units
			*/
		float armLength() const						{ return armLength_; }

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
				Sets the point the camera orbits - the player's feet, once one exists.
			@param target
				World position
			*/
		void setTarget(const glm::vec3& target)		{ target_ = target; }

		/*!
			@brief
				Sets the distance to settle at.

				The camera eases towards this rather than snapping, which is what
				makes the GDD's "pull back during traversal and large casts" read
				as deliberate.
			@param distance
				Desired distance, clamped to the controller's range
			*/
		void setDesiredDistance(float distance)		{ desiredDistance_ = distance; }

		/*!
			@brief
				Sets the offset from the target to the orbit pivot.
			@param o
				World-space offset (chest height by default)
			*/
		void setPivotOffset(const glm::vec3& o)		{ pivotOffset_ = o; }

		/*!
			@brief
				Sets the over-the-shoulder offset of the eye.
			@param o
				x along camera right, y along world up
			*/
		void setShoulderOffset(const glm::vec3& o)	{ shoulderOffset_ = o; }

		/*!
			@brief
				Sets the allowed orbit distance range.
			@param mn
				Minimum distance
			@param mx
				Maximum distance
			*/
		void setDistanceRange(float mn, float mx)	{ minDistance_ = mn; maxDistance_ = mx; }

		/*!
			@brief
				Sets whether vertical look is inverted.
			@param invert
				true to invert the Y axis
			*/
		void setInvertY(bool invert)				{ invertY_ = invert; }

		/*!
			@brief
				Sets the stick look turn rate at full deflection.
			@param radPerSec
				Radians per second
			*/
		void setStickLookSpeed(float radPerSec)		{ stickLookSpeed_ = radPerSec; }

		/*!
			@brief
				Point the camera orbits.
			@return
				Target world position
			*/
		const glm::vec3& target() const				{ return target_; }

		/*!
			@brief
				Distance the camera is easing towards.
			@return
				Desired distance
			*/
		float desiredDistance() const				{ return desiredDistance_; }

		/*!
			@brief
				Drops all smoothing so the camera is exactly where it belongs on
				the next frame.

				Call after a teleport or respawn, otherwise the camera eases across
				the whole level to catch up.
			*/
		void snapToTarget();

	private:

		glm::vec3	target_				{ 0.f };
		glm::vec3	pivotOffset_		{ 0.f, 1.6f, 0.f };		// chest height
		glm::vec3	shoulderOffset_		{ 0.5f, 0.f, 0.f };		// along camera right
		glm::vec3	smoothedPivot_		{ 0.f };

		float		distance_			{ 4.f };
		float		desiredDistance_	{ 4.f };
		float		minDistance_		{ 1.5f };
		float		maxDistance_		{ 9.f };

		float		yaw_				{ 0.f };
		float		pitch_				{ 0.f };
		float		pitchMin_			{ -0.6108652f };		// -35 degrees
		float		pitchMax_			{ 1.2217305f };			// +70 degrees

		float		lookSensitivity_	{ 0.0022f };			// radians per PIXEL
		float		stickLookSpeed_		{ 2.6f };				// radians per SECOND
		float		scrollStep_			{ 0.6f };				// units per wheel tick
		float		stickZoomSpeed_		{ 4.f };				// units per second held
		float		pivotHalfLife_		{ 0.08f };
		float		distanceHalfLife_	{ 0.25f };

		// Spring arm. Pulls in instantly when obstructed - a camera that eases
		// in shows the inside of the wall first - and eases back out.
		CollisionProbe	probe_			{};
		float		probeRadius_		{ 0.2f };
		float		armLength_			{ 0.f };
		float		lastIdealArm_		{ 0.f };
		float		armRecoverHalfLife_	{ 0.2f };

		bool		invertY_			{ false };
		bool		initialized_		{ false };
	};
}
