/*************************************************************************************************************
\file       physics_self_check.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares the startup self-check that verifies the physics integration against the real
            Jolt world.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once


namespace events	{ class Bus; }
namespace ecs		{ class ECSManager; }
namespace physics	{ class System; }


namespace engine::checks
{
	/*!
		@brief
			Verifies the physics integration at startup against the real world.

			Conversions, the layer matrix, resting contact, contact event
			counts, queries, interpolation, body lifetime and the character's
			jump latch - the things that go subtly wrong rather than crash.
			Uses temporary entities and destroys them. Reports to stderr; never
			throws.
		@param phys
			Initialised physics system
		@param ecsManager
			Registry the temporary entities are created in
		@param bus
			Bus physics publishes on
		*/
	void runPhysicsSelfCheck(physics::System& phys, ecs::ECSManager& ecsManager, events::Bus& bus);
}
