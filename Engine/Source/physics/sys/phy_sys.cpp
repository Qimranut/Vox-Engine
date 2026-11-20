/*************************************************************************************************************
\file       phy_sys.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Implements physics::System: Jolt bring-up and teardown, the fixed step (bodies, characters,
            world update, contact publishing), interpolation, ray/sphere queries, body control and
            terrain static mesh bodies.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "phy_sys.hpp"

// Physics
#include "../layers/layers.hpp"
#include "../math/jolt_glm.hpp"
#include "../contact/contact_listener.hpp"
#include "../ecs/body_sync.hpp"
#include "../character/character_controller.hpp"
#include "../events/physics_events.hpp"
#include "../shape/cooked_mesh.hpp"

// Profiling
#include "../../profile/profiler.hpp"

// Components
#include "../../components/component_types/transform.hpp"
#include "../../components/component_types/character_body.hpp"

// Jolt
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/CollisionCollector.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

// stds
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <iostream>
#include <thread>


namespace physics
{
	namespace
	{
		/*!
			@brief
				Jolt trace hook: formats the message and prints it with a [jolt] prefix.
			@param fmt
				printf-style format string
			@param ...
				Format arguments
			*/
		void traceImpl(const char* fmt, ...)
		{
			va_list list;
			va_start(list, fmt);
			char buffer[1024];
			vsnprintf(buffer, sizeof(buffer), fmt, list);
			va_end(list);

			std::cout << "[jolt] " << buffer << "\n";
		}

#ifdef JPH_ENABLE_ASSERTS
		/*!
			@brief
				Jolt assert hook: logs the failed assertion.
			@param expression
				The asserted expression
			@param message
				Optional message, may be null
			@param file
				Source file of the assert
			@param line
				Source line of the assert
			@return
				true, which breaks into the debugger
			*/
		bool assertFailedImpl(const char* expression, const char* message, const char* file, JPH::uint line)
		{
			std::cerr << "[jolt] ASSERT FAILED " << file << ":" << line << ": (" << expression << ") "
					  << (message != nullptr ? message : "") << "\n";

			// true breaks into the debugger
			return true;
		}
#endif

		/*!
			@brief
				Recovers the owning entity from a Jolt body's user data.
			@param user
				Body user data, holding the entity's integral value
			@return
				The entity
			*/
		entt::entity entityFromUserData(JPH::uint64 user)
		{
			return static_cast<entt::entity>(static_cast<std::uint32_t>(user));
		}

		/**
		 * Closest swept hit that is not already overlapping at the start.
		 *
		 * A start overlap reports fraction 0 and would pin a spring arm at
		 * zero length the moment its pivot touched anything.
		 *
		 */
		class ClosestSweepCollector final : public JPH::CastShapeCollector
		{
		public:

			/*!
				@brief
					Keeps the hit if it is nearer than the best so far and not a start overlap.
				@param hit
					Shape cast result reported by Jolt
				*/
			void AddHit(const JPH::ShapeCastResult& hit) override
			{
				if (hit.mFraction <= 0.f)
					return;

				if (!found_ || hit.mFraction < hit_.mFraction)
				{
					hit_ = hit;
					found_ = true;
					UpdateEarlyOutFraction(hit.mFraction);
				}
			}

			bool					found_	{ false };
			JPH::ShapeCastResult	hit_	{};
		};
	}

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
	System::System(::events::Bus& bus, ecs::ECSManager& ecs, const PhysicsSettings& settings)
		:
		bus_{ bus },
		ecs_{ ecs },
		settings_{ settings }
	{}

	/*!
		@brief
			Shuts the world down.
		*/
	System::~System()
	{
		shutdown();
	}

	/*!
		@brief
			Brings up Jolt: allocator, trace/assert hooks, factory, job system,
			layer tables, the world and the body/character hooks. No-op if already up.
		*/
	void System::init()
	{
		if (initialized_)
			return;

		// Before ANY Jolt allocation - including the world object itself,
		// which is why it is a unique_ptr and not a member.
		JPH::RegisterDefaultAllocator();

		JPH::Trace = traceImpl;
		JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = assertFailedImpl;)

		JPH::Factory::sInstance = new JPH::Factory();
		JPH::RegisterTypes();

		tempAllocator_ = std::make_unique<JPH::TempAllocatorImpl>(settings_.tempAllocatorBytes_);

		// Leave a core for the main thread and one for the scheduler's
		// workers (audio). Jolt's own threads do the heavy lifting.
		const int hw = static_cast<int>(std::thread::hardware_concurrency());

		// Two-step construction, because the thread init function has to be
		// set before Init starts the threads: it names each one for the
		// profiler and the debugger.
		auto pool = std::make_unique<JPH::JobSystemThreadPool>();
		pool->SetThreadInitFunction([](int index) { profile::nameThread("Jolt worker", index); });
		pool->Init(
			JPH::cMaxPhysicsJobs,
			JPH::cMaxPhysicsBarriers,
			std::max(1, hw - 2));
		jobSystem_ = std::move(pool);

		layers_ = std::make_unique<layers::LayerTables>();
		contacts_ = std::make_unique<ContactQueue>();

		world_ = std::make_unique<JPH::PhysicsSystem>();
		world_->Init(
			settings_.maxBodies_,
			0,									// body mutexes: 0 = Jolt's default
			settings_.maxBodyPairs_,
			settings_.maxContacts_,
			layers_->bpInterface(),
			layers_->objVsBp(),
			layers_->pairFilter());

		world_->SetGravity(toJolt(settings_.gravity_));
		world_->SetContactListener(contacts_.get());

		bodies_ = std::make_unique<BodySync>(ecs_.registry(), *world_);

		// on_update too: replace fires no on_destroy, so the old controller
		// would stay in the world beside the one the next step creates.
		ecs_.registry().on_destroy<cpn::CharacterBody>().connect<&System::onCharacterRemovedOrReplaced>(*this);
		ecs_.registry().on_update<cpn::CharacterBody>().connect<&System::onCharacterRemovedOrReplaced>(*this);

		initialized_ = true;

		std::cout << "[physics] Jolt " << JPH_VERSION_MAJOR << "." << JPH_VERSION_MINOR << "." << JPH_VERSION_PATCH
				  << " initialized, " << std::max(1, hw - 2) << " worker threads\n";
	}

	/*!
		@brief
			Removes every character and body, then tears down the world and Jolt. Idempotent.
		*/
	void System::shutdown()
	{
		if (!initialized_)
			return;

		auto& registry = ecs_.registry();

		registry.on_destroy<cpn::CharacterBody>().disconnect<&System::onCharacterRemovedOrReplaced>(*this);
		registry.on_update<cpn::CharacterBody>().disconnect<&System::onCharacterRemovedOrReplaced>(*this);

		// Every body out of the world before the world goes. Characters
		// remove their own inner bodies in their destructors.
		characters_.clear();

		for (auto [entity, cb] : registry.view<cpn::CharacterBody>().each())
			cb.handle_ = 0xffffffff;

		bodies_->removeAll();
		bodies_.reset();

		world_.reset();
		contacts_.reset();
		layers_.reset();
		jobSystem_.reset();
		tempAllocator_.reset();

		JPH::UnregisterTypes();

		delete JPH::Factory::sInstance;
		JPH::Factory::sInstance = nullptr;

		pairs_.clear();
		drained_.clear();

		initialized_ = false;
	}

	// ============================ Simulation ============================

	/*!
		@brief
			Advances the world by one fixed step: creates pending bodies, steps
			characters, updates Jolt, reads back poses and publishes events.
		@param dt
			Fixed timestep in seconds
		*/
	void System::step(float dt)
	{
		if (!initialized_)
			return;

		VOX_PROFILE_FUNCTION_C(profile::colour::kPhysics);

		{
			VOX_PROFILE_SCOPE("Create pending bodies");
			bodies_->createPending();
			createPendingCharacters();
		}

		bodies_->beginStep();

		// ---------------------------- Characters ----------------------------
		//
		// Before the world update: a character moves its inner body through
		// the locking body interface, which is not allowed mid-update.
		//
		// Events are collected, not published, so a handler that destroys a
		// character cannot erase from characters_ while it is being walked.

		if (!inputEnabled_)
			intent_.jumpPending_ = false;

		struct CharacterEvent
		{
			entt::entity						entity_;
			character::Controller::StepResult	result_;
		};

		std::vector<CharacterEvent> characterEvents;

		for (auto& [handle, c] : characters_)
		{
			VOX_PROFILE_SCOPE("Character step");

			c->beginStep();

			character::Intent* in = (c->playerControlled() && inputEnabled_) ? &intent_ : nullptr;

			const auto r = c->step(dt, in, *tempAllocator_);

			if (r.jumped_ || r.groundChanged_)
				characterEvents.push_back({ c->entity(), r });
		}

		// ------------------------------ World -------------------------------

		// Jolt's own scopes (and its job threads) appear under this zone:
		// see profile/jolt_profile.cpp.
		JPH::EPhysicsUpdateError err{};
		{
			VOX_PROFILE_SCOPE("Jolt update");
			err = world_->Update(dt, settings_.collisionSteps_, tempAllocator_.get(), jobSystem_.get());
		}

		if (err != JPH::EPhysicsUpdateError::None)
		{
			// Almost always a preallocated buffer running out - the settings
			// in PhysicsSettings are too small for the scene.
			static bool reported = false;

			if (!reported)
			{
				std::cerr << "[physics] update error " << static_cast<int>(err)
						  << " - raise maxBodyPairs_ / maxContacts_ in PhysicsSettings\n";
				reported = true;
			}
		}

		{
			VOX_PROFILE_SCOPE("Read back poses");
			bodies_->endStep();
		}

		// ------------------------------ Events ------------------------------

		for (const auto& ce : characterEvents)
		{
			if (ce.result_.jumped_)
				bus_.publish(events::CharacterJumped{ ce.entity_ });

			if (ce.result_.groundChanged_)
				bus_.publish(events::CharacterGroundChanged{
					ce.entity_, ce.result_.grounded_, ce.result_.verticalSpeed_ });
		}

		publishContacts();
	}

	/*!
		@brief
			Drains the contact queue, folds sub-shape records into body pairs
			and publishes contact and trigger events.
		*/
	void System::publishContacts()
	{
		// Handlers run inside here - gameplay and Lua reacting to contacts.
		VOX_PROFILE_SCOPE_C("Publish contacts", profile::colour::kPhysics);

		contacts_->drain(drained_);

		for (const ContactRecord& r : drained_)
		{
			const std::uint64_t key = (static_cast<std::uint64_t>(r.body1_) << 32) | r.body2_;

			if (r.kind_ == ContactRecord::Kind::Added)
			{
				PairState& p = pairs_[key];

				if (p.count_++ != 0)
					continue;

				p.a_			= entityFromUserData(r.user1_);
				p.b_			= entityFromUserData(r.user2_);
				p.sensor_		= r.sensor_;
				p.aIsSensor_	= r.body1IsSensor_;

				const PairState snapshot = p;

				if (snapshot.sensor_)
				{
					bus_.publish(events::TriggerEntered{
						snapshot.aIsSensor_ ? snapshot.a_ : snapshot.b_,
						snapshot.aIsSensor_ ? snapshot.b_ : snapshot.a_ });
				}
				else
				{
					bus_.publish(events::ContactBegan{
						snapshot.a_, snapshot.b_, r.point_, r.normal_, r.approachSpeed_ });
				}
			}
			else
			{
				const auto it = pairs_.find(key);

				// Removal of a pair we never saw added: it began before a
				// shutdown/restart. Nothing to end.
				if (it == pairs_.end())
					continue;

				if (--it->second.count_ != 0)
					continue;

				const PairState snapshot = it->second;
				pairs_.erase(it);

				if (snapshot.sensor_)
				{
					bus_.publish(events::TriggerExited{
						snapshot.aIsSensor_ ? snapshot.a_ : snapshot.b_,
						snapshot.aIsSensor_ ? snapshot.b_ : snapshot.a_ });
				}
				else
				{
					bus_.publish(events::ContactEnded{ snapshot.a_, snapshot.b_ });
				}
			}
		}
	}

	/*!
		@brief
			Writes interpolated poses into Transforms and publishes
			CharacterPoseUpdated for every character.
		@param alpha
			Leftover accumulator / dt, clamped to [0, 1]
		*/
	void System::interpolate(float alpha)
	{
		if (!initialized_)
			return;

		VOX_PROFILE_FUNCTION_C(profile::colour::kPhysics);

		alpha = std::clamp(alpha, 0.f, 1.f);

		bodies_->interpolate(alpha);

		auto& registry = ecs_.registry();

		std::vector<events::CharacterPoseUpdated> poses;
		poses.reserve(characters_.size());

		for (auto& [handle, c] : characters_)
		{
			const glm::vec3 centre = c->interpolatedCentre(alpha);

			if (auto* tf = registry.try_get<cpn::Transform>(c->entity()))
				tf->pos_ = centre;

			poses.push_back({
				c->entity(),
				centre - glm::vec3{ 0.f, c->halfHeight(), 0.f },
				c->velocity(),
				c->grounded(),
				c->playerControlled() });
		}

		for (const auto& pose : poses)
			bus_.publish(pose);
	}

	/*!
		@brief
			Creates pending bodies, then rebuilds the broad phase for fast queries.
		*/
	void System::optimizeBroadPhase()
	{
		if (!initialized_)
			return;

		// Bodies still waiting for their first step are not in the tree yet.
		bodies_->createPending();
		world_->OptimizeBroadPhase();
	}

	/*!
		@brief
			Creates a character controller for every CharacterBody without one.
		*/
	void System::createPendingCharacters()
	{
		auto& registry = ecs_.registry();

		for (auto [entity, cb, tf] : registry.view<cpn::CharacterBody, cpn::Transform>().each())
		{
			if (cb.handle_ != 0xffffffff)
				continue;

			const std::uint32_t handle = nextCharacterHandle_++;

			characters_.emplace(handle,
				std::make_unique<character::Controller>(*world_, entity, cb, tf.pos_));

			cb.handle_ = handle;
		}
	}

	/*!
		@brief
			Drops the controller of an entity whose CharacterBody was destroyed
			or replaced, and resets its handle.
		@param registry
			Registry that fired the hook
		@param entity
			Entity whose CharacterBody changed
		*/
	void System::onCharacterRemovedOrReplaced(entt::registry& registry, entt::entity entity)
	{
		// By entity, never by handle_: after a replace that holds the new
		// value's handle, not the controller in the world. Characters are
		// few, so the scan is cheap.
		std::erase_if(characters_, [entity](const auto& entry)
		{
			return entry.second->entity() == entity;
		});

		// A replaced component is rebuilt by the next createPendingCharacters.
		registry.get<cpn::CharacterBody>(entity).handle_ = 0xffffffff;
	}

	// ============================= Queries ==============================

	/*!
		@brief
			Closest ray hit against the layers the filter accepts.
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
	std::optional<RayHit> System::raycast(const glm::vec3& origin,
										  const glm::vec3& direction,
										  float maxDistance,
										  QueryFilter filter) const
	{
		const float len = glm::length(direction);

		if (!initialized_ || len < 1e-6f || maxDistance <= 0.f)
			return std::nullopt;

		const glm::vec3 dir = direction / len;

		const JPH::RRayCast ray{ toJolt(origin), toJolt(dir * maxDistance) };
		JPH::RayCastResult hit;

		const std::uint32_t mask = layers::queryMask(filter);
		const layers::MaskBroadPhaseLayerFilter bpFilter{ mask };
		const layers::MaskObjectLayerFilter objFilter{ mask };

		if (!world_->GetNarrowPhaseQuery().CastRay(ray, hit, bpFilter, objFilter))
			return std::nullopt;

		RayHit out;
		out.distance_ = hit.mFraction * maxDistance;
		out.point_ = origin + dir * out.distance_;

		JPH::BodyLockRead lock{ world_->GetBodyLockInterface(), hit.mBodyID };

		if (lock.Succeeded())
		{
			const JPH::Body& body = lock.GetBody();

			out.normal_ = toGlm(body.GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, toJolt(out.point_)));
			out.entity_ = entityFromUserData(body.GetUserData());
		}

		return out;
	}

	/*!
		@brief
			Closest hit of a sphere swept along a line, ignoring start overlaps.
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
	std::optional<RayHit> System::sphereCast(const glm::vec3& origin,
											 const glm::vec3& direction,
											 float maxDistance,
											 float radius,
											 QueryFilter filter) const
	{
		const float len = glm::length(direction);

		if (!initialized_ || len < 1e-6f || maxDistance <= 0.f || radius <= 0.f)
			return std::nullopt;

		const glm::vec3 dir = direction / len;

		// Stack shape: embedded, so no reference count ever tries to free it.
		JPH::SphereShape sphere{ radius };
		sphere.SetEmbedded();

		const JPH::RShapeCast cast = JPH::RShapeCast::sFromWorldTransform(
			&sphere,
			JPH::Vec3::sOne(),
			JPH::RMat44::sTranslation(toJolt(origin)),
			toJolt(dir * maxDistance));

		JPH::ShapeCastSettings castSettings;

		const std::uint32_t mask = layers::queryMask(filter);
		const layers::MaskBroadPhaseLayerFilter bpFilter{ mask };
		const layers::MaskObjectLayerFilter objFilter{ mask };

		ClosestSweepCollector collector;

		world_->GetNarrowPhaseQuery().CastShape(
			cast, castSettings, JPH::RVec3::sZero(), collector, bpFilter, objFilter);

		if (!collector.found_)
			return std::nullopt;

		const JPH::ShapeCastResult& hit = collector.hit_;

		RayHit out;
		out.distance_ = hit.mFraction * maxDistance;
		out.point_ = toGlm(hit.mContactPointOn2);
		out.normal_ = glm::normalize(-toGlm(hit.mPenetrationAxis));

		JPH::BodyLockRead lock{ world_->GetBodyLockInterface(), hit.mBodyID2 };

		if (lock.Succeeded())
			out.entity_ = entityFromUserData(lock.GetBody().GetUserData());

		return out;
	}

	// ============================== Bodies ==============================

	/*!
		@brief
			Applies an impulse to a dynamic body. No-op for other bodies.
		@param entity
			Entity owning the body
		@param impulse
			Impulse in world space, kg m/s
		*/
	void System::addImpulse(entt::entity entity, const glm::vec3& impulse)
	{
		if (!initialized_)
			return;

		const JPH::BodyID id = bodies_->bodyOf(entity);
		JPH::BodyInterface& bi = world_->GetBodyInterface();

		if (!id.IsInvalid() && bi.GetMotionType(id) == JPH::EMotionType::Dynamic)
			bi.AddImpulse(id, toJolt(impulse));
	}

	/*!
		@brief
			Sets the linear velocity of a non-static body.
		@param entity
			Entity owning the body
		@param velocity
			New velocity in world space, m/s
		*/
	void System::setLinearVelocity(entt::entity entity, const glm::vec3& velocity)
	{
		if (!initialized_)
			return;

		const JPH::BodyID id = bodies_->bodyOf(entity);
		JPH::BodyInterface& bi = world_->GetBodyInterface();

		if (!id.IsInvalid() && bi.GetMotionType(id) != JPH::EMotionType::Static)
			bi.SetLinearVelocity(id, toJolt(velocity));
	}

	/*!
		@brief
			Linear velocity of a rigid body or character.
		@param entity
			Entity owning the body or character
		@return
			The velocity, or zero when the entity has neither
		*/
	glm::vec3 System::linearVelocity(entt::entity entity) const
	{
		if (!initialized_)
			return glm::vec3{ 0.f };

		const JPH::BodyID id = bodies_->bodyOf(entity);

		if (!id.IsInvalid())
			return toGlm(world_->GetBodyInterface().GetLinearVelocity(id));

		if (const auto* cb = ecs_.registry().try_get<cpn::CharacterBody>(entity))
		{
			const auto it = characters_.find(cb->handle_);

			if (it != characters_.end())
				return it->second->velocity();
		}

		return glm::vec3{ 0.f };
	}

	/*!
		@brief
			Moves a rigid body, or else a character, instantly.
		@param entity
			Entity owning the body or character
		@param pos
			Body origin, or capsule centre for a character
		@param rot
			Ignored for characters
		@return
			false when the entity has neither
		*/
	bool System::teleport(entt::entity entity, const glm::vec3& pos, const glm::quat& rot)
	{
		if (!initialized_)
			return false;

		if (bodies_->teleport(entity, pos, rot))
			return true;

		auto& registry = ecs_.registry();

		if (const auto* cb = registry.try_get<cpn::CharacterBody>(entity))
		{
			const auto it = characters_.find(cb->handle_);

			if (it != characters_.end())
			{
				it->second->teleport(pos);

				if (auto* tf = registry.try_get<cpn::Transform>(entity))
					tf->pos_ = pos;

				return true;
			}
		}

		return false;
	}

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
			Time to reach the target
		@return
			false when not initialized or the entity has no body
		*/
	bool System::moveKinematic(entt::entity entity, const glm::vec3& pos, const glm::quat& rot, float dt)
	{
		return initialized_ && bodies_->moveKinematic(entity, pos, rot, dt);
	}

	/*!
		@brief
			Sets the world gravity, applied now if initialized and kept for init().
		@param g
			Gravity vector, m/s^2
		*/
	void System::setGravity(const glm::vec3& g)
	{
		settings_.gravity_ = g;

		if (initialized_)
			world_->SetGravity(toJolt(g));
	}

	/*!
		@brief
			Current world gravity.
		@return
			The gravity vector, m/s^2
		*/
	glm::vec3 System::gravity() const
	{
		return settings_.gravity_;
	}

	/*!
		@brief
			Rigid bodies owned by the ECS bridge, characters excluded.
		@return
			The body count, 0 before init
		*/
	std::size_t System::bodyCount() const
	{
		return bodies_ ? bodies_->bodyCount() : 0;
	}

	// ======================= Static meshes (terrain) ====================

	/*!
		@brief
			Adds a sleeping static body around a cooked mesh, outside the ECS bridge.
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
	std::uint32_t System::addStaticMesh(const CookedMesh& mesh, const glm::vec3& position, entt::entity owner, float friction)
	{
		if (!initialized_ || mesh.shape() == nullptr)
			return kInvalidBodyId;

		JPH::BodyCreationSettings settings(
			mesh.shape(),
			toJolt(position),
			JPH::Quat::sIdentity(),
			JPH::EMotionType::Static,
			layers::toObjectLayer(Layer::Static));

		settings.mUserData = static_cast<JPH::uint64>(entt::to_integral(owner));
		settings.mFriction = friction;

		const JPH::BodyID id = world_->GetBodyInterface().CreateAndAddBody(settings, JPH::EActivation::DontActivate);
		if (id.IsInvalid())
		{
			std::cerr << "[physics] out of bodies for terrain - raise PhysicsSettings::maxBodies_\n";
			return kInvalidBodyId;
		}

		++staticMeshes_;
		return id.GetIndexAndSequenceNumber();
	}

	/*!
		@brief
			Removes and destroys a body from addStaticMesh(). No-op for invalid,
			failed or unadded ids.
		@param bodyId
			Body id returned by addStaticMesh()
		*/
	void System::removeBody(std::uint32_t bodyId)
	{
		if (!initialized_ || bodyId == kInvalidBodyId || bodyId == kFailedBodyId)
			return;

		JPH::BodyInterface& bi = world_->GetBodyInterface();
		const JPH::BodyID id(bodyId);
		if (!bi.IsAdded(id))
			return;

		bi.RemoveBody(id);
		bi.DestroyBody(id);
		if (staticMeshes_ > 0)
			--staticMeshes_;
	}
}
