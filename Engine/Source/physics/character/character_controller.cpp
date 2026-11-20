/*************************************************************************************************************
\file       character_controller.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Character controller implementation: capsule and CharacterVirtual setup, the per-step
            horizontal acceleration, gravity, coyote-time and buffered jumps, and teleport.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "character_controller.hpp"

// Physics
#include "../math/jolt_glm.hpp"
#include "../layers/layers.hpp"

// Jolt
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Collision/ShapeFilter.h>

// stds
#include <algorithm>
#include <cmath>


namespace physics::character
{
	namespace
	{
		// A jump pressed this long after walking off a ledge still counts.
		constexpr float kCoyoteTime = 0.1f;

		// Vertical closing speed below which the character is treated as
		// resting on the ground rather than rising away from it.
		constexpr float kTowardsGroundEpsilon = 0.1f;
	}

	/*!
		@brief
			Creates the CharacterVirtual (capsule lifted so its bottom is the origin) and its inner body.
		@param system
			World to live in. Must outlive the controller.
		@param entity
			Owning entity, stored as the inner body's user data
		@param cfg
			Size and movement tuning, copied and sanitised
		@param centre
			Initial capsule centre, world space
		*/
	Controller::Controller(JPH::PhysicsSystem& system,
						   entt::entity entity,
						   const cpn::CharacterBody& cfg,
						   const glm::vec3& centre)
		:
		system_{ system },
		entity_{ entity },
		cfg_{ cfg }
	{
		const float radius = std::max(cfg_.radius_, 0.05f);
		const float height = std::max(cfg_.height_, 2.f * radius + 0.01f);
		const float cylinderHalf = 0.5f * height - radius;

		cfg_.radius_ = radius;
		cfg_.height_ = height;

		// CharacterVirtual expects the shape's BOTTOM at its origin, so its
		// position is the feet. The capsule is lifted by half its height.
		JPH::RefConst<JPH::Shape> shape = JPH::RotatedTranslatedShapeSettings(
			JPH::Vec3(0.f, 0.5f * height, 0.f),
			JPH::Quat::sIdentity(),
			new JPH::CapsuleShape(cylinderHalf, radius)).Create().Get();

		JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
		settings->mShape				= shape;
		settings->mMaxSlopeAngle		= JPH::DegreesToRadians(cfg_.maxSlopeDeg_);

		// Only contacts below the lower hemisphere's centre can hold the
		// character up; anything above is a wall or a ceiling.
		settings->mSupportingVolume		= JPH::Plane(JPH::Vec3::sAxisY(), -radius);

		settings->mInnerBodyShape		= shape;
		settings->mInnerBodyLayer		= layers::toObjectLayer(Layer::Character);

		// Voxel terrain is thousands of triangle meshes butted together;
		// without this, the edges where two sections meet register as
		// tiny ledges and catch the capsule at speed.
		settings->mEnhancedInternalEdgeRemoval = true;

		const glm::vec3 feet = centre + feetFromCentre();

		character_ = new JPH::CharacterVirtual(
			settings,
			toJolt(feet),
			JPH::Quat::sIdentity(),
			static_cast<JPH::uint64>(entt::to_integral(entity)),
			&system_);

		prevCentre_ = centre;
		currCentre_ = centre;
	}

	/*!
		@brief
			Advances the character one fixed step: horizontal wish velocity, gravity and jumps, then ExtendedUpdate.
		@param dt
			Fixed timestep in seconds
		@param intent
			Player intent, consumed; nullptr for an unsteered character
		@param allocator
			Temporary allocator for Jolt's collision queries
		@return
			Jump and ground-change edges for the caller to publish
		*/
	Controller::StepResult Controller::step(float dt, Intent* intent, JPH::TempAllocator& allocator)
	{
		StepResult result;

		const JPH::Vec3 up = character_->GetUp();
		const JPH::Vec3 gravity = system_.GetGravity();

		// The ground may have moved during the last world update (a platform,
		// a body the character stands on).
		character_->UpdateGroundVelocity();

		const bool onGround =
			character_->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;

		coyoteTimer_ = onGround ? kCoyoteTime : std::max(0.f, coyoteTimer_ - dt);

		// ------------------------- Horizontal -------------------------

		glm::vec3 wish{ 0.f };
		bool sprint = false;

		if (intent)
		{
			glm::vec2 mv = intent->move_;

			// Clamp, don't normalize: a keyboard diagonal must not be faster,
			// but a half-pushed stick must still walk slowly.
			const float len2 = glm::dot(mv, mv);
			if (len2 > 1.f)
				mv /= std::sqrt(len2);

			// Flattened camera basis - see camera.hpp for the yaw convention.
			const float yaw = intent->headingYaw_;
			const glm::vec3 right{ std::cos(yaw), 0.f, -std::sin(yaw) };
			const glm::vec3 forward{ -std::sin(yaw), 0.f, -std::cos(yaw) };

			wish = right * mv.x + forward * mv.y;
			sprint = intent->sprint_;
		}

		const float speed = sprint ? cfg_.sprintSpeed_ : cfg_.walkSpeed_;
		const float accel = cfg_.groundAccel_ * (onGround ? 1.f : cfg_.airControl_);

		// Exponential approach: frame-rate independent, unlike a fixed lerp.
		desiredHorizontal_ = glm::mix(desiredHorizontal_, wish * speed, 1.f - std::exp(-accel * dt));

		// -------------------------- Vertical --------------------------

		const JPH::Vec3 linear = character_->GetLinearVelocity();
		const JPH::Vec3 verticalVel = up * linear.Dot(up);
		const JPH::Vec3 groundVel = character_->GetGroundVelocity();

		const bool movingTowardsGround =
			(verticalVel.GetY() - groundVel.GetY()) < kTowardsGroundEpsilon;

		// Standing: inherit the ground's velocity, which also cancels the
		// gravity accumulated last step. Airborne: keep falling.
		JPH::Vec3 newVel = (onGround && movingTowardsGround) ? groundVel : verticalVel;

		if (intent && intent->jumpPending_)
		{
			if (coyoteTimer_ > 0.f && movingTowardsGround)
			{
				const JPH::Vec3 base = onGround ? groundVel : JPH::Vec3::sZero();

				newVel = base;
				newVel.SetY(base.GetY() + cfg_.jumpSpeed_);

				intent->jumpPending_ = false;
				coyoteTimer_ = 0.f;
				result.jumped_ = true;
			}
			else
			{
				// Buffered: keep the request alive briefly so a press just
				// before landing is not lost.
				intent->jumpBuffer_ -= dt;

				if (intent->jumpBuffer_ <= 0.f)
					intent->jumpPending_ = false;
			}
		}

		newVel += gravity * dt;
		newVel += toJolt(desiredHorizontal_);

		character_->SetLinearVelocity(newVel);

		// --------------------------- Move -----------------------------

		JPH::CharacterVirtual::ExtendedUpdateSettings ext;

		// Sticking to the floor on the step we jump would pull us straight
		// back down.
		if (result.jumped_)
			ext.mStickToFloorStepDown = JPH::Vec3::sZero();

		const JPH::ObjectLayer layer = layers::toObjectLayer(Layer::Character);

		character_->ExtendedUpdate(
			dt,
			gravity,
			ext,
			system_.GetDefaultBroadPhaseLayerFilter(layer),
			system_.GetDefaultLayerFilter(layer),
			JPH::BodyFilter{},
			JPH::ShapeFilter{},
			allocator);

		currCentre_ = toGlm(JPH::Vec3(character_->GetPosition())) - feetFromCentre();

		const bool groundedNow =
			character_->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;

		if (groundedNow != grounded_)
		{
			result.groundChanged_ = true;
			result.verticalSpeed_ = newVel.GetY();
		}

		grounded_ = groundedNow;
		result.grounded_ = grounded_;

		return result;
	}

	/*!
		@brief
			Moves the character instantly, dropping velocity and interpolation.
		@param centre
			New capsule centre, world space
		*/
	void Controller::teleport(const glm::vec3& centre)
	{
		character_->SetPosition(toJolt(centre + feetFromCentre()));
		character_->SetLinearVelocity(JPH::Vec3::sZero());

		desiredHorizontal_ = glm::vec3{ 0.f };
		prevCentre_ = centre;
		currCentre_ = centre;
	}

	/*!
		@brief
			Capsule centre blended between the last two steps.
		@param alpha
			0 = previous step, 1 = current step
		@return
			World position
		*/
	glm::vec3 Controller::interpolatedCentre(float alpha) const
	{
		return glm::mix(prevCentre_, currCentre_, alpha);
	}

	/*!
		@brief
			The character's current linear velocity.
		@return
			Velocity in metres per second, world space
		*/
	glm::vec3 Controller::velocity() const
	{
		return toGlm(character_->GetLinearVelocity());
	}
}
