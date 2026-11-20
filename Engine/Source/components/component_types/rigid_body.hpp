/*************************************************************************************************************
\file       rigid_body.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      cpn::RigidBody, a Jolt rigid body attached to an entity: its authoring values (motion,
            layer, mass, friction, damping, sensor, continuous) and the runtime state physics owns.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Physics types - Jolt-free on purpose, see physics_types.hpp
#include "../../physics/types/physics_types.hpp"

// GLM
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// stds
#include <cstdint>


namespace cpn
{
	/**
	 * A Jolt rigid body attached to an entity.
	 *
	 * Needs a cpn::Collider and a cpn::Transform on the same entity. The body
	 * is created lazily by physics::System at the start of the next fixed step
	 * that finds all three, so component order does not matter, and destroyed
	 * when this component (or the entity) is destroyed.
	 *
	 * For Dynamic bodies, Transform is OUTPUT: physics writes the interpolated
	 * pose into it every frame, and writing it yourself does nothing. Move a
	 * dynamic body with physics::System::teleport / addImpulse /
	 * setLinearVelocity, and a kinematic one with moveKinematic.
	 *
	 */
	struct RigidBody
	{
		// ------------------------- Authoring --------------------------

		physics::MotionType	motion_			{ physics::MotionType::Dynamic };
		physics::Layer		layer_			{ physics::Layer::Dynamic };

		// kg. 0 = computed from the shape's volume at 1000 kg/m^3.
		float				mass_			{ 0.f };

		float				friction_		{ 0.5f };
		float				restitution_	{ 0.f };
		float				linearDamping_	{ 0.05f };
		float				angularDamping_	{ 0.05f };
		float				gravityFactor_	{ 1.f };

		// Trigger volume: reports TriggerEntered / TriggerExited, never pushes.
		bool				isSensor_		{ false };

		bool				allowSleeping_	{ true };

		// Swept collision (Jolt LinearCast). For anything fast and small
		// enough to tunnel through a wall in one step - crossbow bolts.
		bool				continuous_		{ false };

		// -------------------------- Runtime ---------------------------
		// Owned by physics::System. Do not write.

		std::uint32_t		bodyId_			{ physics::kInvalidBodyId };

		glm::vec3			prevPos_		{ 0.f };
		glm::vec3			currPos_		{ 0.f };
		glm::quat			prevRot_		{ 1.f, 0.f, 0.f, 0.f };
		glm::quat			currRot_		{ 1.f, 0.f, 0.f, 0.f };
	};
}
