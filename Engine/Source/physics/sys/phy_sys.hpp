/*************************************************************************************************************
\file       phy_sys.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares physics::System, the Jolt-backed physics world: fixed-step simulation, render
            interpolation, ray and sphere queries, body control, terrain static meshes and characters.
            Jolt types are forward declared so nothing outside physics can reach them.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Physics
#include "../types/physics_types.hpp"
#include "../character/character_intent.hpp"

// Event bus
#include "../../events/bus.hpp"

// ECS
#include "../../ecs/ecs_manager/ecs_mng.hpp"

// GLM
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// stds
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>


// Jolt, forward declared: the engine's glue includes this header, and nothing outside
// physics should be able to reach a Jolt type through it.
namespace JPH
{
	class PhysicsSystem;
	class TempAllocator;
	class JobSystem;
}

namespace physics
{
	namespace layers	{ class LayerTables; }
	namespace character	{ class Controller; }

	class ContactQueue;
	class BodySync;
	class CookedMesh;
	struct ContactRecord;

	/**
	 * World sizing. Jolt preallocates everything from these at init.
	 *
	 */
	struct PhysicsSettings
	{
		std::uint32_t	maxBodies_			{ 16384 };
		std::uint32_t	maxBodyPairs_		{ 65536 };
		std::uint32_t	maxContacts_		{ 16384 };
		std::size_t		tempAllocatorBytes_	{ 32 * 1024 * 1024 };

		// Collision sub-iterations per step. 1 is right for a 100 Hz step;
		// raise it only if the step grows past ~1/60 s.
		int				collisionSteps_		{ 1 };

		glm::vec3		gravity_			{ 0.f, -9.81f, 0.f };
	};

	/**
	 * The physics world, backed by Jolt.
	 *
	 * Stepped from the engine's FIXED timestep on the main thread - not
	 * through the scheduler. Jolt spreads each step across its own job
	 * thread pool, so a scheduler worker would only add a thread waiting on
	 * those jobs, and the per-render-frame scheduler would tie simulation
	 * speed to frame rate.
	 *
	 * One frame, as the engine drives it:
	 *
	 *   input_->update()          character intent filled by bus subscribers
	 *   repeat 0..N times:
	 *     step(dt)                bodies created, characters moved, world
	 *                             stepped, contacts published on the bus
	 *   interpolate(alpha)        Transforms written, CharacterPoseUpdated
	 *   graphics                  camera follows the pose, spring arm queries
	 *                             sphereCast()
	 *
	 * Main thread only, like the bus it publishes on.
	 *
	 */
	class System
	{
	public:

		/*!
			@brief
				Stores the bus, ECS and settings. Jolt is not touched until init().
			@param bus
				Event bus contact and character events are published on
			@param ecs
				ECS whose RigidBody / CharacterBody entities become bodies
			@param settings
				World sizing and gravity
			*/
		System(::events::Bus& bus, ecs::ECSManager& ecs, const PhysicsSettings& settings = {});

		/*!
			@brief
				Shuts the world down.
			*/
		~System();

		System(const System&) = delete;
		System& operator=(const System&) = delete;

		/*!
			@brief
				Brings up Jolt: allocator, factory, job system and the world.
			*/
		void init();

		/*!
			@brief
				Tears the world down. Idempotent.
			*/
		void shutdown();

		// ================== Simulation ==================

		/*!
			@brief
				Advances the world by one fixed step and publishes its events.
			@param dt
				Fixed timestep in seconds
			*/
		void step(float dt);

		/*!
			@brief
				Writes render poses between the last two steps into every
				Transform, and publishes CharacterPoseUpdated.
			@param alpha
				Leftover accumulator / dt, in [0, 1]
			*/
		void interpolate(float alpha);

		/*!
			@brief
				Rebuilds the broad phase for fast queries.

				Call once after loading a batch of bodies, never per frame.
			*/
		void optimizeBroadPhase();

		// ==================== Queries ===================

		/*!
			@brief
				Closest ray hit.
			@param origin
				Ray start in world space
			@param direction
				Need not be normalized
			@param maxDistance
				Longest distance the ray travels
			@param filter
				Which layers can be hit
			@return
				The hit, or nothing
			*/
		std::optional<RayHit> raycast(const glm::vec3& origin,
									  const glm::vec3& direction,
									  float maxDistance,
									  QueryFilter filter = QueryFilter::Gameplay) const;

		/*!
			@brief
				Closest hit of a sphere swept along a line.

				Hits that already overlap at the start are ignored - a camera
				whose pivot is grazing a wall must still be able to pull back.
			@param origin
				Sphere centre at the start
			@param direction
				Sweep direction, need not be normalized
			@param maxDistance
				Longest distance the sphere travels
			@param radius
				Sphere radius
			@param filter
				Which layers can be hit
			@return
				The hit, with distance_ the distance the sphere centre can travel
			*/
		std::optional<RayHit> sphereCast(const glm::vec3& origin,
										 const glm::vec3& direction,
										 float maxDistance,
										 float radius,
										 QueryFilter filter = QueryFilter::Gameplay) const;

		// ==================== Bodies ====================

		/*!
			@brief
				Applies an impulse to a dynamic body. No-op for other bodies.
			@param entity
				Entity owning the body
			@param impulse
				Impulse in world space, kg m/s
			*/
		void addImpulse(entt::entity entity, const glm::vec3& impulse);

		/*!
			@brief
				Sets the linear velocity of a non-static body.
			@param entity
				Entity owning the body
			@param velocity
				New velocity in world space, m/s
			*/
		void setLinearVelocity(entt::entity entity, const glm::vec3& velocity);

		/*!
			@brief
				Linear velocity of a rigid body or character.
			@param entity
				Entity owning the body or character
			@return
				The velocity, or zero when the entity has neither
			*/
		glm::vec3 linearVelocity(entt::entity entity) const;

		/*!
			@brief
				Moves a rigid body or character instantly, without interpolation.
			@param entity
				Entity owning the body or character
			@param pos
				Body origin, or capsule centre for a character
			@param rot
				Ignored for characters
			@return
				false when the entity has neither
			*/
		bool teleport(entt::entity entity, const glm::vec3& pos, const glm::quat& rot = { 1.f, 0.f, 0.f, 0.f });

		/*!
			@brief
				Drives a kinematic body to a target pose over dt.
			@param entity
				Entity owning the body
			@param pos
				Target world position
			@param rot
				Target world rotation
			@param dt
				Time to reach the target, normally the fixed timestep
			@return
				false when not initialized or the entity has no body
			*/
		bool moveKinematic(entt::entity entity, const glm::vec3& pos, const glm::quat& rot, float dt);

		/*!
			@brief
				Sets the world gravity, applied now if initialized and kept for init().
			@param g
				Gravity vector, m/s^2
			*/
		void setGravity(const glm::vec3& g);

		/*!
			@brief
				Current world gravity.
			@return
				The gravity vector, m/s^2
			*/
		glm::vec3 gravity() const;

		/*!
			@brief
				Rigid bodies owned by the ECS bridge, characters excluded.
			@return
				The body count, 0 before init
			*/
		std::size_t bodyCount() const;

		// ================= Static meshes (terrain) ================

		/*!
			@brief
				Adds a static body around a cooked triangle mesh, outside the
				ECS bridge.

				Terrain sections use this: thousands of bodies that share one
				owner entity and never move.
			@param mesh
				Cooked mesh shape
			@param position
				World position of the mesh's local origin
			@param owner
				Entity reported by raycast() and contact events for this body
			@param friction
				Surface friction of the body
			@return
				Body id for removeBody(), or kInvalidBodyId
			*/
		std::uint32_t addStaticMesh(const CookedMesh& mesh, const glm::vec3& position, entt::entity owner, float friction = 0.6f);

		/*!
			@brief
				Removes and destroys a body from addStaticMesh(). No-op for
				kInvalidBodyId.
			@param bodyId
				Body id returned by addStaticMesh()
			*/
		void removeBody(std::uint32_t bodyId);

		/*!
			@brief
				Bodies currently alive from addStaticMesh().
			@return
				The static mesh body count
			*/
		std::size_t staticMeshCount() const { return staticMeshes_; }

		// ================== Character ===================

		/*!
			@brief
				Intent the player-controlled character consumes next step.
			@return
				Mutable intent, filled by the engine's input subscribers
			*/
		character::Intent& characterIntent() { return intent_; }

		/*!
			@brief
				Gates player intent, e.g. off while the debug fly camera is active.

				A disabled character still falls and collides.
			@param enabled
				Whether the player character reads intent
			*/
		void setCharacterInputEnabled(bool enabled) { inputEnabled_ = enabled; }

	private:

		::events::Bus&		bus_;
		ecs::ECSManager&	ecs_;
		PhysicsSettings		settings_;

		bool	initialized_	{ false };

		// Destruction order matters: the world references the layer tables,
		// and every body must be gone before the world is.
		std::unique_ptr<JPH::TempAllocator>			tempAllocator_;
		std::unique_ptr<JPH::JobSystem>				jobSystem_;
		std::unique_ptr<layers::LayerTables>		layers_;
		std::unique_ptr<ContactQueue>				contacts_;
		std::unique_ptr<JPH::PhysicsSystem>			world_;
		std::unique_ptr<BodySync>					bodies_;

		// ------------------------- Characters -------------------------

		std::unordered_map<std::uint32_t, std::unique_ptr<character::Controller>>	characters_;
		std::uint32_t	nextCharacterHandle_	{ 0 };

		character::Intent	intent_			{};
		bool				inputEnabled_	{ true };

		std::size_t			staticMeshes_	{ 0 };

		// --------------------------- Contacts ---------------------------

		struct PairState
		{
			std::uint32_t	count_		{ 0 };
			entt::entity	a_			{ entt::null };
			entt::entity	b_			{ entt::null };
			bool			sensor_		{ false };
			bool			aIsSensor_	{ false };
		};

		// Body-pair key -> live sub-shape contact count. Jolt reports
		// sub-shape pairs; gameplay wants one Began / Ended per body pair.
		std::unordered_map<std::uint64_t, PairState>	pairs_;
		std::vector<ContactRecord>						drained_;

		/*!
			@brief
				Creates a character controller for every CharacterBody without one.
			*/
		void createPendingCharacters();

		/*!
			@brief
				Drops the controller of an entity whose CharacterBody was destroyed
				or replaced, and resets its handle.
			@param registry
				Registry that fired the hook
			@param entity
				Entity whose CharacterBody changed
			*/
		void onCharacterRemovedOrReplaced(entt::registry& registry, entt::entity entity);

		/*!
			@brief
				Drains the contact queue, folds sub-shape records into body pairs
				and publishes contact and trigger events.
			*/
		void publishContacts();
	};
}
