/*************************************************************************************************************
\file       character_controller.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      physics::character::Controller: a capsule character on JPH::CharacterVirtual with an inner
            kinematic body, stepped before PhysicsSystem::Update, with walk / sprint, coyote time and
            buffered jumps, teleport and interpolated position.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Character
#include "character_intent.hpp"

// Components
#include "../../components/component_types/character_body.hpp"

// Jolt
#include <Jolt/Jolt.h>
#include <Jolt/Core/Reference.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>

// GLM
#include <glm/glm.hpp>

// Entt
#include <entt.hpp>


namespace physics::character
{
	/**
	 * A capsule character built on JPH::CharacterVirtual.
	 *
	 * CharacterVirtual rather than a dynamic rigid capsule because a player
	 * needs to stop dead, turn instantly, walk up stairs and stick to slopes -
	 * all of which a rigid body fights. It is not a body in the physics
	 * world, so it also carries an INNER kinematic body on Layer::Character:
	 * that is what enemy raycasts, bolts and trigger volumes see.
	 *
	 * Stepped on the main thread BEFORE PhysicsSystem::Update, which is the
	 * order Jolt requires - the inner body is moved through the locking body
	 * interface, which must not be used while the world is updating.
	 *
	 */
	class Controller
	{
	public:

		/**
		 * What happened during one step, for the caller to publish.
		 *
		 */
		struct StepResult
		{
			bool	jumped_			{ false };
			bool	groundChanged_	{ false };
			bool	grounded_		{ false };
			float	verticalSpeed_	{ 0.f };
		};

		/*!
			@brief
				Creates the character and its inner body.
			@param system
				World to live in. Must outlive the controller.
			@param entity
				Owning entity, stored as the inner body's user data
			@param cfg
				Size and movement tuning, copied
			@param centre
				Initial capsule centre, world space
			*/
		Controller(JPH::PhysicsSystem& system,
				   entt::entity entity,
				   const cpn::CharacterBody& cfg,
				   const glm::vec3& centre);

		Controller(const Controller&) = delete;
		Controller& operator=(const Controller&) = delete;

		/*!
			@brief
				Advances the character by one fixed step.
			@param dt
				Fixed timestep in seconds
			@param intent
				Player intent, consumed (a pending jump is cleared when used or
				expired). nullptr for a character nobody is steering - it still
				falls and stays on the ground.
			@param allocator
				Temporary allocator for Jolt's collision queries
			@return
				Edges for the caller to publish
			*/
		StepResult step(float dt, Intent* intent, JPH::TempAllocator& allocator);

		/*!
			@brief
				Remembers the current position as the previous one. Call before
				each fixed step.
			*/
		void beginStep() { prevCentre_ = currCentre_; }

		/*!
			@brief
				Moves the character instantly, dropping velocity and interpolation.
			@param centre
				New capsule centre, world space
			*/
		void teleport(const glm::vec3& centre);

		/*!
			@brief
				Capsule centre between the last two steps.
			@param alpha
				0 = previous step, 1 = current step
			@return
				World position
			*/
		glm::vec3 interpolatedCentre(float alpha) const;

		/*!
			@brief
				Capsule centre after the latest step.
			@return
				World position
			*/
		glm::vec3		centre() const		{ return currCentre_; }
		/*!
			@brief
				The character's current linear velocity.
			@return
				Velocity in metres per second, world space
			*/
		glm::vec3		velocity() const;
		/*!
			@brief
				Whether the character stood on the ground after the latest step.
			@return
				true when on the ground
			*/
		bool			grounded() const	{ return grounded_; }
		/*!
			@brief
				Half the capsule's total height.
			@return
				Distance from centre to feet in metres
			*/
		float			halfHeight() const	{ return cfg_.height_ * 0.5f; }
		/*!
			@brief
				Owning entity.
			@return
				The entity this character belongs to
			*/
		entt::entity	entity() const		{ return entity_; }
		/*!
			@brief
				Whether the player's intent steers this character.
			@return
				The component's playerControlled flag
			*/
		bool			playerControlled() const { return cfg_.playerControlled_; }
		/*!
			@brief
				Jolt id of the inner kinematic body.
			@return
				The inner body's BodyID
			*/
		JPH::BodyID		innerBodyId() const	{ return character_->GetInnerBodyID(); }

	private:

		JPH::PhysicsSystem&					system_;
		JPH::Ref<JPH::CharacterVirtual>		character_;
		entt::entity						entity_;
		cpn::CharacterBody					cfg_;

		// Smoothed horizontal velocity the player is asking for. Kept apart
		// from the character's real velocity, which walls and slopes clip.
		glm::vec3	desiredHorizontal_	{ 0.f };

		glm::vec3	prevCentre_			{ 0.f };
		glm::vec3	currCentre_			{ 0.f };

		bool		grounded_			{ false };
		float		coyoteTimer_		{ 0.f };

		/*!
			@brief
				Offset from the capsule centre down to the feet.
			@return
				(0, -halfHeight, 0)
			*/
		glm::vec3	feetFromCentre() const	{ return { 0.f, -halfHeight(), 0.f }; }
	};
}
