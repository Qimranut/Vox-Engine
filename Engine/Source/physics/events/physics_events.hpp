/*************************************************************************************************************
\file       physics_events.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Bus events published by physics::System on the main thread: contact begin/end, trigger
            enter/exit, and character jump, ground-change and per-frame pose updates.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// GLM
#include <glm/glm.hpp>

// Entt
#include <entt.hpp>


// ---------------------------------------------------------------------------
//  Physics events.
//
//  Every one of these is published on the MAIN thread by physics::System -
//  never from a Jolt callback. Jolt reports contacts from its job threads,
//  and the bus is main-thread only, so the contact listener only enqueues
//  and System::step() drains the queue after PhysicsSystem::Update returns.
//  Same shape as input's RawEvent queue.
//
//  Contact and trigger events are published from inside the FIXED timestep,
//  so a render frame may carry zero or several steps' worth. Character pose
//  is the exception: it is interpolated and published once per render frame.
//
//  Bodies are identified by entity. An entity may already be destroyed by
//  the time its ContactEnded arrives - check registry.valid() before using
//  it.
// ---------------------------------------------------------------------------

namespace physics::events
{
	// ----------------------------- Contacts -----------------------------

	/**
	 * Two bodies started touching.
	 *
	 * Body-pair level: a box resting on two triangles of a mesh is one
	 * contact, not two. Entities are ordered by Jolt body id, so either may be
	 * the one you care about.
	 *
	 */
	struct ContactBegan
	{
		entt::entity	a_				{ entt::null };
		entt::entity	b_				{ entt::null };

		glm::vec3		point_			{ 0.f };	// world space, on a_'s surface
		glm::vec3		normal_			{ 0.f };	// world space, from a_ towards b_

		// Closing speed along the normal before the solver resolved the hit,
		// m/s, positive when approaching. The number for impact audio volume
		// and destruction thresholds.
		float			approachSpeed_	{ 0.f };
	};

	/**
	 * Two bodies stopped touching.
	 *
	 * ALSO published when either body falls asleep - Jolt only tracks
	 * contacts of active bodies - and when either is destroyed. A pile that
	 * settles ends its contacts; it begins them again when woken.
	 *
	 */
	struct ContactEnded
	{
		entt::entity	a_				{ entt::null };
		entt::entity	b_				{ entt::null };
	};

	/**
	 * A body entered a sensor.
	 *
	 */
	struct TriggerEntered
	{
		entt::entity	trigger_		{ entt::null };
		entt::entity	other_			{ entt::null };
	};

	/**
	 * A body left a sensor, went to sleep inside it, or was destroyed.
	 *
	 */
	struct TriggerExited
	{
		entt::entity	trigger_		{ entt::null };
		entt::entity	other_			{ entt::null };
	};

	// ----------------------------- Character ----------------------------

	/**
	 * A character left the ground under its own jump.
	 *
	 */
	struct CharacterJumped
	{
		entt::entity	entity_			{ entt::null };
	};

	/**
	 * A character landed or lost its footing.
	 *
	 */
	struct CharacterGroundChanged
	{
		entt::entity	entity_			{ entt::null };
		bool			grounded_		{ false };

		// Vertical velocity at the moment of the change. On landing, the
		// negative of the fall speed - use it for landing effects.
		float			verticalSpeed_	{ 0.f };
	};

	/**
	 * Where a character is this render frame.
	 *
	 * Published once per frame after interpolation, so it is smooth at any
	 * refresh rate. This is the event the orbit camera follows.
	 *
	 */
	struct CharacterPoseUpdated
	{
		entt::entity	entity_			{ entt::null };
		glm::vec3		feet_			{ 0.f };
		glm::vec3		velocity_		{ 0.f };
		bool			grounded_		{ false };
		bool			playerControlled_	{ false };
	};
}
