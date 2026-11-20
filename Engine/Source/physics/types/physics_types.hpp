/*************************************************************************************************************
\file       physics_types.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Jolt-free physics vocabulary used by components and callers: motion types, layers, shapes,
            query filters, ray hit results and the reserved body id values.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// GLM
#include <glm/glm.hpp>

// Entt
#include <entt.hpp>

// stds
#include <cstdint>


// ---------------------------------------------------------------------------
//  Engine-side physics vocabulary.
//
//  Deliberately NO Jolt include. Components (cpn::RigidBody, cpn::Collider)
//  use these enums, and the component list reaches almost every translation
//  unit in the engine - one Jolt include here would drag Jolt into all of
//  them. Jolt conversions live in physics/math/jolt_glm.hpp and
//  physics/layers/layers.hpp, which only physics sources include.
// ---------------------------------------------------------------------------

namespace physics
{
	/**
	 * How a body moves.
	 *
	 */
	enum class MotionType : std::uint8_t
	{
		Static,		// never moves - arena shell, indestructible supports
		Kinematic,	// moved by gameplay through moveKinematic(), pushes dynamics
		Dynamic		// simulated
	};

	/**
	 * Collision layer. Which layers collide is decided by one table in
	 * layers.hpp - see shouldCollide().
	 *
	 */
	enum class Layer : std::uint16_t
	{
		Static,		// arena shell and structural supports
		Dynamic,	// simulated props, enemies' rigid parts, projectiles
		Character,	// the CharacterVirtual inner body, so rays and bolts hit the player
		Debris,		// destruction chunks: hit the world, never trip the player
		Sensor,		// trigger volumes

		Count
	};

	/**
	 * Collision shape of a cpn::Collider.
	 *
	 */
	enum class ShapeType : std::uint8_t
	{
		Box,
		Sphere,
		Capsule
	};

	/**
	 * Which layers a scene query can hit.
	 *
	 */
	enum class QueryFilter : std::uint8_t
	{
		Camera,		// Static + Dynamic: the spring arm ignores the player and chunks
		Gameplay,	// Static + Dynamic + Character
		All
	};

	/**
	 * Result of a ray or shape cast.
	 *
	 */
	struct RayHit
	{
		glm::vec3		point_		{ 0.f };
		glm::vec3		normal_		{ 0.f, 1.f, 0.f };
		float			distance_	{ 0.f };
		entt::entity	entity_		{ entt::null };
	};

	/** JPH::BodyID::cInvalidBodyID, restated so components need no Jolt include. */
	inline constexpr std::uint32_t kInvalidBodyId = 0xffffffff;

	/**
	 * Body id of a component whose shape could not be built. Distinct from
	 * kInvalidBodyId so the failure is reported once rather than retried and
	 * logged every step. Never a real Jolt id: those have bit 31 clear.
	 */
	inline constexpr std::uint32_t kFailedBodyId = 0xfffffffe;

	/*!
		@brief
			Whether a stored body id refers to a live Jolt body.
		@param id
			Stored body id
		@return
			false for kInvalidBodyId and kFailedBodyId
		*/
	inline constexpr bool hasBody(std::uint32_t id)
	{
		return id != kInvalidBodyId && id != kFailedBodyId;
	}
}
