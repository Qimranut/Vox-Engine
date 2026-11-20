/*************************************************************************************************************
\file       body_sync.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares physics::BodySync, which keeps Jolt rigid bodies in step with cpn::RigidBody entities:
            lazy body creation, pose read-back, render interpolation and the entity <-> body mapping.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Components
#include "../../components/component_types/transform.hpp"
#include "../../components/component_types/rigid_body.hpp"
#include "../../components/component_types/collider.hpp"

// Jolt
#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyID.h>

// Entt
#include <entt.hpp>

// GLM
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// stds
#include <cstdint>
#include <unordered_map>
#include <vector>


namespace physics
{
	/**
	 * Keeps Jolt rigid bodies in step with cpn::RigidBody entities.
	 *
	 * Owns the entity <-> body mapping and the pose history used for render
	 * interpolation. Main thread only, and never during
	 * PhysicsSystem::Update.
	 *
	 */
	class BodySync
	{
	public:

		/*!
			@brief
				Connects the RigidBody destroy/update hooks to the registry.
			@param registry
				Registry holding the RigidBody entities
			@param system
				Jolt physics system the bodies live in
			*/
		BodySync(entt::registry& registry, JPH::PhysicsSystem& system);

		/*!
			@brief
				Disconnects the RigidBody hooks from the registry.
			*/
		~BodySync();

		BodySync(const BodySync&) = delete;
		BodySync& operator=(const BodySync&) = delete;

		/*!
			@brief
				Builds a body for every entity that has RigidBody, Collider and
				Transform but no body yet.

				Lazy on purpose: archetypes attach components in hash-map order,
				so an on_construct hook for RigidBody could fire before the
				Collider exists.
			*/
		void createPending();

		/*!
			@brief
				Rolls current poses into previous poses. Before each fixed step.
			*/
		void beginStep();

		/*!
			@brief
				Reads back the pose of every body the step moved. After each fixed
				step.
			*/
		void endStep();

		/*!
			@brief
				Writes the render pose of every non-static body into its Transform.
			@param alpha
				Fraction of a fixed step elapsed since the last one, [0, 1]
			*/
		void interpolate(float alpha);

		/*!
			@brief
				Removes and destroys every body. Components are kept, with their
				body ids reset.
			*/
		void removeAll();

		/*!
			@brief
				Moves a body instantly, with no interpolation smear.
			@param entity
				Entity whose body is moved
			@param pos
				New world position
			@param rot
				New world rotation
			@return
				false when the entity has no body
			*/
		bool teleport(entt::entity entity, const glm::vec3& pos, const glm::quat& rot);

		/*!
			@brief
				Drives a kinematic body to a target pose over one step.
			@param entity
				Entity whose body is moved
			@param pos
				Target world position
			@param rot
				Target world rotation
			@param dt
				Time to reach the target, normally the fixed timestep
			@return
				false when the entity has no body
			*/
		bool moveKinematic(entt::entity entity, const glm::vec3& pos, const glm::quat& rot, float dt);

		/*!
			@brief
				Body of an entity.
			@param entity
				Entity to look up
			@return
				The body id, invalid when the entity has none
			*/
		JPH::BodyID bodyOf(entt::entity entity) const;

		/*!
			@brief
				Entity owning a live body.
			@param id
				Jolt body id to look up
			@return
				The entity, or entt::null
			*/
		entt::entity entityOf(JPH::BodyID id) const;

		/*!
			@brief
				Number of bodies this sync currently owns.
			@return
				The live body count
			*/
		std::size_t bodyCount() const { return entities_.size(); }

	private:

		entt::registry&		registry_;
		JPH::PhysicsSystem&	system_;

		std::unordered_map<std::uint32_t, entt::entity>	entities_;

		// The reverse of entities_. A replace overwrites RigidBody::bodyId_
		// before any hook runs, so the old body can only be found from here.
		std::unordered_map<entt::entity, std::uint32_t>	bodies_;

		// Entities whose body moved in the last step.
		std::vector<entt::entity>	moved_;
		JPH::BodyIDVector			active_;

		/*!
			@brief
				Drops the entity's body when its RigidBody is destroyed, replaced
				or patched.

				A replaced component is rebuilt by the next createPending, from its
				new values and the current Transform.
			@param registry
				Registry that fired the hook
			@param entity
				Entity whose RigidBody changed
			*/
		void onRemovedOrReplaced(entt::registry& registry, entt::entity entity);

		/*!
			@brief
				Destroys the body mapped to an entity and forgets the mapping.
			@param entity
				Entity whose body is destroyed
			*/
		void destroyBodyOf(entt::entity entity);

		/*!
			@brief
				Removes a body from the world (if added) and destroys it.
			@param bodyId
				Jolt index-and-sequence number of the body
			*/
		void destroyBody(std::uint32_t bodyId);
	};
}
