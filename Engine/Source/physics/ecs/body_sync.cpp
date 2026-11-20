/*************************************************************************************************************
\file       body_sync.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Implements physics::BodySync: builds Jolt shapes and bodies for RigidBody entities, reads back
            poses after each step, interpolates render poses and tears bodies down on component changes.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "body_sync.hpp"

// Physics
#include "../math/jolt_glm.hpp"
#include "../layers/layers.hpp"

// Jolt
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>

// stds
#include <algorithm>
#include <iostream>


namespace physics
{
	namespace
	{
		/*!
			@brief
				Maps the engine's motion type to Jolt's.
			@param m
				Engine motion type
			@return
				The matching Jolt motion type (Dynamic for unknown values)
			*/
		JPH::EMotionType toJolt(MotionType m)
		{
			switch (m)
			{
			case MotionType::Static:	return JPH::EMotionType::Static;
			case MotionType::Kinematic:	return JPH::EMotionType::Kinematic;
			case MotionType::Dynamic:
			default:					return JPH::EMotionType::Dynamic;
			}
		}

		/*!
			@brief
				Builds the Jolt shape for a collider at a given scale.
			@param col
				Collider describing the shape, size and offset
			@param scale
				Transform scale, baked into the shape's dimensions
			@return
				The shape, or null with the error logged
			*/
		JPH::RefConst<JPH::Shape> buildShape(const cpn::Collider& col, const glm::vec3& scale)
		{
			const glm::vec3 s = glm::abs(scale);
			const float minSize = 0.001f;

			JPH::ShapeSettings::ShapeResult result;

			switch (col.shape_)
			{
			case ShapeType::Sphere:
			{
				const float r = std::max(col.radius_ * std::max({ s.x, s.y, s.z }), minSize);
				result = JPH::SphereShapeSettings(r).Create();
				break;
			}

			case ShapeType::Capsule:
			{
				const float r = std::max(col.radius_ * std::max(s.x, s.z), minSize);
				const float h = std::max(col.halfHeight_ * s.y, minSize);
				result = JPH::CapsuleShapeSettings(h, r).Create();
				break;
			}

			case ShapeType::Box:
			default:
			{
				const glm::vec3 he = glm::max(col.halfExtents_ * s, glm::vec3{ minSize });

				// Jolt rejects a convex radius larger than the smallest half
				// extent, which a thin wall or floor tile easily has.
				const float convexRadius = std::min(JPH::cDefaultConvexRadius,
					0.5f * std::min({ he.x, he.y, he.z }));

				result = JPH::BoxShapeSettings(physics::toJolt(he), convexRadius).Create();
				break;
			}
			}

			if (result.HasError())
			{
				std::cerr << "[physics] shape creation failed: " << result.GetError().c_str() << "\n";
				return nullptr;
			}

			if (col.offset_ == glm::vec3{ 0.f })
				return result.Get();

			JPH::ShapeSettings::ShapeResult offset = JPH::RotatedTranslatedShapeSettings(
				physics::toJolt(col.offset_ * s),
				JPH::Quat::sIdentity(),
				result.Get()).Create();

			if (offset.HasError())
			{
				std::cerr << "[physics] offset shape creation failed: " << offset.GetError().c_str() << "\n";
				return nullptr;
			}

			return offset.Get();
		}
	}

	/*!
		@brief
			Connects the RigidBody destroy/update hooks to the registry.
		@param registry
			Registry holding the RigidBody entities
		@param system
			Jolt physics system the bodies live in
		*/
	BodySync::BodySync(entt::registry& registry, JPH::PhysicsSystem& system)
		:
		registry_{ registry },
		system_{ system }
	{
		// on_update as well as on_destroy: replace (which archetype
		// modifyComponent uses) fires no on_destroy, and without this the old
		// body stayed in the world while createPending built a second one.
		registry_.on_destroy<cpn::RigidBody>().connect<&BodySync::onRemovedOrReplaced>(*this);
		registry_.on_update<cpn::RigidBody>().connect<&BodySync::onRemovedOrReplaced>(*this);
	}

	/*!
		@brief
			Disconnects the RigidBody hooks from the registry.
		*/
	BodySync::~BodySync()
	{
		registry_.on_destroy<cpn::RigidBody>().disconnect<&BodySync::onRemovedOrReplaced>(*this);
		registry_.on_update<cpn::RigidBody>().disconnect<&BodySync::onRemovedOrReplaced>(*this);
	}

	/*!
		@brief
			Builds a body for every entity with RigidBody, Collider and Transform
			but no body yet.
		*/
	void BodySync::createPending()
	{
		JPH::BodyInterface& bi = system_.GetBodyInterface();

		auto view = registry_.view<cpn::RigidBody, cpn::Collider, cpn::Transform>();

		for (auto [entity, rb, col, tf] : view.each())
		{
			if (rb.bodyId_ != kInvalidBodyId)
				continue;

			JPH::RefConst<JPH::Shape> shape = buildShape(col, tf.scale_);
			if (shape == nullptr)
			{
				// Reported once, not every step.
				rb.bodyId_ = kFailedBodyId;
				continue;
			}

			// A sensor on a solid layer would push things it should only
			// detect, so the flag wins.
			const Layer layer = rb.isSensor_ ? Layer::Sensor : rb.layer_;

			JPH::BodyCreationSettings settings(
				shape,
				physics::toJolt(tf.pos_),
				physics::toJolt(glm::normalize(tf.rot_)),
				toJolt(rb.motion_),
				layers::toObjectLayer(layer));

			settings.mUserData			= static_cast<JPH::uint64>(entt::to_integral(entity));
			settings.mFriction			= rb.friction_;
			settings.mRestitution		= rb.restitution_;
			settings.mLinearDamping		= rb.linearDamping_;
			settings.mAngularDamping	= rb.angularDamping_;
			settings.mGravityFactor		= rb.gravityFactor_;
			settings.mAllowSleeping		= rb.allowSleeping_;
			settings.mIsSensor			= rb.isSensor_;

			// Lets a static or kinematic sensor see the character's kinematic
			// inner body, which it otherwise ignores.
			settings.mCollideKinematicVsNonDynamic = rb.isSensor_;

			if (rb.continuous_)
				settings.mMotionQuality = JPH::EMotionQuality::LinearCast;

			if (rb.mass_ > 0.f && rb.motion_ == MotionType::Dynamic)
			{
				settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
				settings.mMassPropertiesOverride.mMass = rb.mass_;
			}

			const JPH::EActivation activation = (rb.motion_ == MotionType::Static)
				? JPH::EActivation::DontActivate
				: JPH::EActivation::Activate;

			const JPH::BodyID id = bi.CreateAndAddBody(settings, activation);

			if (id.IsInvalid())
			{
				std::cerr << "[physics] out of bodies - raise PhysicsSettings::maxBodies_\n";
				break;
			}

			rb.bodyId_	= id.GetIndexAndSequenceNumber();
			rb.prevPos_	= tf.pos_;
			rb.currPos_	= tf.pos_;
			rb.prevRot_	= tf.rot_;
			rb.currRot_	= tf.rot_;

			entities_[rb.bodyId_] = entity;
			bodies_[entity] = rb.bodyId_;
		}
	}

	/*!
		@brief
			Rolls current poses into previous poses for the bodies that moved last step.
		*/
	void BodySync::beginStep()
	{
		for (entt::entity e : moved_)
		{
			if (auto* rb = registry_.try_get<cpn::RigidBody>(e))
			{
				rb->prevPos_ = rb->currPos_;
				rb->prevRot_ = rb->currRot_;
			}
		}

		moved_.clear();
	}

	/*!
		@brief
			Reads back the pose of every active body the step moved.
		*/
	void BodySync::endStep()
	{
		system_.GetActiveBodies(JPH::EBodyType::RigidBody, active_);

		// No other thread touches the world between updates, so the
		// non-locking interface is safe here and much cheaper per body.
		const JPH::BodyInterface& bi = system_.GetBodyInterfaceNoLock();

		for (const JPH::BodyID& id : active_)
		{
			const auto it = entities_.find(id.GetIndexAndSequenceNumber());
			if (it == entities_.end())
				continue;	// a character's inner body, or not ours

			auto* rb = registry_.try_get<cpn::RigidBody>(it->second);
			if (!rb)
				continue;

			JPH::RVec3 pos;
			JPH::Quat rot;
			bi.GetPositionAndRotation(id, pos, rot);

			rb->currPos_ = physics::toGlm(JPH::Vec3(pos));
			rb->currRot_ = physics::toGlm(rot);

			moved_.push_back(it->second);
		}
	}

	/*!
		@brief
			Writes the interpolated render pose of every non-static body into its Transform.
		@param alpha
			Fraction of a fixed step elapsed since the last one, [0, 1]
		*/
	void BodySync::interpolate(float alpha)
	{
		auto view = registry_.view<cpn::RigidBody, cpn::Transform>();

		for (auto [entity, rb, tf] : view.each())
		{
			if (rb.motion_ == MotionType::Static || !hasBody(rb.bodyId_))
				continue;

			tf.pos_ = glm::mix(rb.prevPos_, rb.currPos_, alpha);
			tf.rot_ = glm::slerp(rb.prevRot_, rb.currRot_, alpha);
		}
	}

	/*!
		@brief
			Removes and destroys every body, resetting the components' body ids.
		*/
	void BodySync::removeAll()
	{
		for (const auto& [entity, bodyId] : bodies_)
			destroyBody(bodyId);

		for (auto [entity, rb] : registry_.view<cpn::RigidBody>().each())
			rb.bodyId_ = kInvalidBodyId;

		entities_.clear();
		bodies_.clear();
		moved_.clear();
	}

	/*!
		@brief
			Moves a body instantly, resetting its pose history and Transform.
		@param entity
			Entity whose body is moved
		@param pos
			New world position
		@param rot
			New world rotation
		@return
			false when the entity has no body
		*/
	bool BodySync::teleport(entt::entity entity, const glm::vec3& pos, const glm::quat& rot)
	{
		const JPH::BodyID id = bodyOf(entity);
		if (id.IsInvalid())
			return false;

		auto& rb = registry_.get<cpn::RigidBody>(entity);

		system_.GetBodyInterface().SetPositionAndRotation(
			id, physics::toJolt(pos), physics::toJolt(glm::normalize(rot)),
			rb.motion_ == MotionType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);

		rb.prevPos_ = rb.currPos_ = pos;
		rb.prevRot_ = rb.currRot_ = rot;

		if (auto* tf = registry_.try_get<cpn::Transform>(entity))
		{
			tf->pos_ = pos;
			tf->rot_ = rot;
		}

		return true;
	}

	/*!
		@brief
			Drives a kinematic body to a target pose over dt.
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
	bool BodySync::moveKinematic(entt::entity entity, const glm::vec3& pos, const glm::quat& rot, float dt)
	{
		const JPH::BodyID id = bodyOf(entity);
		if (id.IsInvalid())
			return false;

		system_.GetBodyInterface().MoveKinematic(
			id, physics::toJolt(pos), physics::toJolt(glm::normalize(rot)), dt);

		return true;
	}

	/*!
		@brief
			Body of an entity, from its RigidBody component.
		@param entity
			Entity to look up
		@return
			The body id, invalid when the entity has none
		*/
	JPH::BodyID BodySync::bodyOf(entt::entity entity) const
	{
		const auto* rb = registry_.try_get<cpn::RigidBody>(entity);

		if (!rb || !hasBody(rb->bodyId_))
			return JPH::BodyID{};

		return JPH::BodyID{ rb->bodyId_ };
	}

	/*!
		@brief
			Entity owning a live body.
		@param id
			Jolt body id to look up
		@return
			The entity, or entt::null
		*/
	entt::entity BodySync::entityOf(JPH::BodyID id) const
	{
		const auto it = entities_.find(id.GetIndexAndSequenceNumber());
		return it != entities_.end() ? it->second : entt::null;
	}

	/*!
		@brief
			Drops the entity's body and resets its body id so the next
			createPending rebuilds it.
		@param registry
			Registry that fired the hook
		@param entity
			Entity whose RigidBody changed
		*/
	void BodySync::onRemovedOrReplaced(entt::registry& registry, entt::entity entity)
	{
		destroyBodyOf(entity);

		// Also clears kFailedBodyId, so a replaced collider gets a retry.
		registry.get<cpn::RigidBody>(entity).bodyId_ = kInvalidBodyId;
	}

	/*!
		@brief
			Destroys the body mapped to an entity and forgets the mapping.
		@param entity
			Entity whose body is destroyed
		*/
	void BodySync::destroyBodyOf(entt::entity entity)
	{
		// By entity, never by RigidBody::bodyId_: after a replace that holds
		// the new value's id, not the body in the world.
		const auto it = bodies_.find(entity);
		if (it == bodies_.end())
			return;

		destroyBody(it->second);

		entities_.erase(it->second);
		bodies_.erase(it);
	}

	/*!
		@brief
			Removes a body from the world (if added) and destroys it.
		@param bodyId
			Jolt index-and-sequence number of the body
		*/
	void BodySync::destroyBody(std::uint32_t bodyId)
	{
		const JPH::BodyID id{ bodyId };
		JPH::BodyInterface& bi = system_.GetBodyInterface();

		if (bi.IsAdded(id))
			bi.RemoveBody(id);

		bi.DestroyBody(id);
	}
}
