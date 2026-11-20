/*************************************************************************************************************
\file       physics_self_check.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Startup self-check for the Jolt integration: quaternion order, layer matrix, resting contact,
            queries, interpolation, the camera filter, the character jump latch and body lifetime.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "physics_self_check.hpp"

// Subsystems
#include "../../events/bus.hpp"
#include "../../ecs/ecs_manager/ecs_mng.hpp"
#include "../../physics/sys/phy_sys.hpp"
#include "../../physics/events/physics_events.hpp"
#include "../../physics/character/character_intent.hpp"

// Component registry
#include "../../components/registry/comp_registry.hpp"

// Physics internals. Jolt stays out of every header this file exposes.
#include "../../physics/math/jolt_glm.hpp"
#include "../../physics/layers/layers.hpp"

// GLM
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// stds
#include <cmath>
#include <cstddef>
#include <iostream>


namespace engine::checks
{
	/*!
		@brief
			Verifies the physics integration against the real world with temporary
			entities far from the origin, then destroys them and prints the result.
		@param phys
			Initialised physics system
		@param ecsManager
			Registry the temporary entities are created in
		@param bus
			Bus physics publishes on
		*/
	void runPhysicsSelfCheck(physics::System& phys, ecs::ECSManager& ecsManager, events::Bus& bus)
	{
		namespace pev = physics::events;

		int failures = 0;

		const auto check = [&failures](bool ok, const char* what)
		{
			if (!ok)
			{
				std::cerr << "[physics-check] FAILED: " << what << "\n";
				++failures;
			}
		};

		const auto nearly = [](float a, float b, float eps)
		{
			return std::fabs(a - b) < eps;
		};

		// -------------------- Quaternion order ----------------------
		//
		// glm::quat is (w, x, y, z) and JPH::Quat is (x, y, z, w). Swapping
		// them compiles and rotates everything about the wrong axis.
		{
			const glm::quat q = glm::angleAxis(0.7f, glm::normalize(glm::vec3{ 1.f, 2.f, 3.f }));
			const glm::vec3 v{ 1.f, -2.f, 0.5f };

			const glm::vec3 viaGlm = q * v;
			const glm::vec3 viaJolt = physics::toGlm(physics::toJolt(q) * physics::toJolt(v));
			const glm::quat back = physics::toGlm(physics::toJolt(q));

			check(glm::length(viaGlm - viaJolt) < 1e-4f,
				"glm and Jolt rotate a vector differently - quaternion component order is wrong");

			check(std::fabs(glm::dot(q, back)) > 0.9999f, "quaternion does not round-trip glm -> Jolt -> glm");
		}

		// ---------------------- Layer matrix ------------------------
		{
			using physics::Layer;
			using physics::layers::shouldCollide;

			bool symmetric = true;

			for (int a = 0; a < static_cast<int>(Layer::Count); ++a)
				for (int b = 0; b < static_cast<int>(Layer::Count); ++b)
					symmetric &= shouldCollide(static_cast<Layer>(a), static_cast<Layer>(b))
							  == shouldCollide(static_cast<Layer>(b), static_cast<Layer>(a));

			check(symmetric, "collision matrix is not symmetric");
			check(!shouldCollide(Layer::Static, Layer::Static), "static bodies collide with each other");
			check(!shouldCollide(Layer::Debris, Layer::Character), "debris collides with the character");
			check(shouldCollide(Layer::Character, Layer::Static), "character does not collide with the world");
		}

		// ------------------------ World -----------------------------
		//
		// Far from the origin so nothing here touches the real scene.

		auto& reg = ecsManager.registry();
		const glm::vec3 base{ 1000.f, 0.f, 1000.f };
		const std::size_t baselineBodies = phys.bodyCount();

		const auto makeBody = [&reg](const glm::vec3& pos, const glm::vec3& scale,
									 physics::MotionType motion, physics::Layer layer,
									 physics::ShapeType shape)
		{
			const entt::entity e = reg.create();

			cpn::Transform tf;
			tf.pos_ = pos;
			tf.scale_ = scale;
			reg.emplace<cpn::Transform>(e, tf);

			cpn::RigidBody rb;
			rb.motion_ = motion;
			rb.layer_ = layer;
			reg.emplace<cpn::RigidBody>(e, rb);

			cpn::Collider col;
			col.shape_ = shape;
			reg.emplace<cpn::Collider>(e, col);

			return e;
		};

		const entt::entity ground = makeBody(base + glm::vec3{ 0.f, -0.5f, 0.f }, { 20.f, 1.f, 20.f },
			physics::MotionType::Static, physics::Layer::Static, physics::ShapeType::Box);

		const entt::entity ball = makeBody(base + glm::vec3{ 0.f, 5.f, 0.f }, glm::vec3{ 1.f },
			physics::MotionType::Dynamic, physics::Layer::Dynamic, physics::ShapeType::Sphere);

		int began = 0;
		float approach = 0.f;

		{
			events::ScopedSubscription contactSub{ bus, bus.subscribe<pev::ContactBegan>(
				[&](const pev::ContactBegan& e)
				{
					const bool pair = (e.a_ == ground && e.b_ == ball) || (e.a_ == ball && e.b_ == ground);

					if (pair)
					{
						++began;
						approach = e.approachSpeed_;
					}
				}) };

			// 3 s: a 5 m fall takes ~1 s, the rest lets it settle and sleep.
			for (int i = 0; i < 300; ++i)
				phys.step(0.01f);
		}

		phys.interpolate(1.f);

		check(phys.bodyCount() == baselineBodies + 2, "bodies were not created for RigidBody entities");

		check(nearly(reg.get<cpn::Transform>(ball).pos_.y, 0.5f, 0.02f),
			"dropped sphere does not rest on the ground at y = 0.5");

		check(began == 1, "resting contact did not produce exactly one ContactBegan");
		check(approach > 0.f, "ContactBegan approach speed is not positive for a falling body");

		// ----------------------- Raycast ----------------------------
		{
			const auto hit = phys.raycast(base + glm::vec3{ 3.f, 10.f, 3.f }, { 0.f, -1.f, 0.f }, 50.f);

			check(hit.has_value(), "raycast straight down missed the ground");

			if (hit)
			{
				check(nearly(hit->distance_, 10.f, 0.01f), "raycast distance to the ground is wrong");
				check(hit->entity_ == ground, "raycast did not report the ground entity");
				check(nearly(hit->normal_.y, 1.f, 1e-3f), "raycast normal on flat ground is not +Y");
			}
		}

		// --------------------- Interpolation ------------------------
		{
			phys.setLinearVelocity(ball, { 0.f, 0.f, 5.f });
			phys.step(0.01f);

			const auto& rb = reg.get<cpn::RigidBody>(ball);

			check(glm::length(rb.currPos_ - rb.prevPos_) > 1e-3f, "a moving body did not record a pose change");

			phys.interpolate(0.f);
			check(glm::length(reg.get<cpn::Transform>(ball).pos_ - rb.prevPos_) < 1e-5f,
				"interpolate(0) is not the previous pose");

			phys.interpolate(1.f);
			check(glm::length(reg.get<cpn::Transform>(ball).pos_ - rb.currPos_) < 1e-5f,
				"interpolate(1) is not the current pose");
		}

		// -------------------- Camera filter -------------------------
		//
		// A Character-layer body between the camera and a wall: the spring
		// arm must see through it to the wall.

		const entt::entity wall = makeBody(base + glm::vec3{ -8.f, 2.f, -5.f }, { 4.f, 2.f, 0.2f },
			physics::MotionType::Static, physics::Layer::Static, physics::ShapeType::Box);

		const entt::entity blocker = makeBody(base + glm::vec3{ -8.f, 2.f, -2.f }, glm::vec3{ 1.f },
			physics::MotionType::Kinematic, physics::Layer::Character, physics::ShapeType::Box);

		phys.step(0.01f);

		{
			const glm::vec3 from = base + glm::vec3{ -8.f, 2.f, 0.f };
			const glm::vec3 dir{ 0.f, 0.f, -1.f };

			const auto camHit = phys.sphereCast(from, dir, 10.f, 0.2f, physics::QueryFilter::Camera);
			const auto gameHit = phys.sphereCast(from, dir, 10.f, 0.2f, physics::QueryFilter::Gameplay);

			// wall face at z = -4.9, blocker face at z = -1.5, minus the radius
			check(camHit && nearly(camHit->distance_, 4.7f, 0.05f), "camera sphere cast did not stop at the wall");
			check(camHit && camHit->entity_ == wall, "camera sphere cast hit something other than the wall");
			check(gameHit && nearly(gameHit->distance_, 1.3f, 0.05f), "gameplay sphere cast ignored a Character body");
		}

		// ---------------------- Character ---------------------------
		{
			const entt::entity hero = reg.create();

			cpn::Transform tf;
			tf.pos_ = base + glm::vec3{ 5.f, 1.2f, 5.f };
			reg.emplace<cpn::Transform>(hero, tf);

			cpn::CharacterBody cb;
			cb.playerControlled_ = true;
			reg.emplace<cpn::CharacterBody>(hero, cb);

			int jumps = 0;
			int poses = 0;
			bool grounded = false;

			events::ScopedSubscription jumpSub{ bus, bus.subscribe<pev::CharacterJumped>(
				[&jumps, hero](const pev::CharacterJumped& e)
				{
					if (e.entity_ == hero)
						++jumps;
				}) };

			events::ScopedSubscription poseSub{ bus, bus.subscribe<pev::CharacterPoseUpdated>(
				[&grounded, &poses, hero](const pev::CharacterPoseUpdated& e)
				{
					if (e.entity_ == hero)
					{
						grounded = e.grounded_;
						++poses;
					}
				}) };

			physics::character::Intent& intent = phys.characterIntent();
			intent = {};

			for (int i = 0; i < 100; ++i)
				phys.step(0.01f);

			phys.interpolate(1.f);
			check(grounded, "character did not settle on the ground");

			// One press, three substeps: exactly one jump.
			intent.jumpPending_ = true;
			intent.jumpBuffer_ = physics::character::kJumpBufferSeconds;

			for (int i = 0; i < 3; ++i)
				phys.step(0.01f);

			check(jumps == 1, "one jump press did not produce exactly one CharacterJumped");
			check(!intent.jumpPending_, "jump latch was not consumed");

			// Archetype modifyComponent replaces with the prototype, whose
			// handle_ is unset. That must swap the controller, not add one.
			intent = {};
			reg.replace<cpn::CharacterBody>(hero, cb);
			phys.step(0.01f);

			poses = 0;
			phys.interpolate(1.f);
			check(poses == 1, "replacing a CharacterBody left a second controller in the world");

			intent = {};
			reg.destroy(hero);
		}

		// ----------------------- Lifetime ---------------------------
		//
		// replace fires no on_destroy, and the new value carries no body id.
		// The old body must still leave the world, and one new body appear.
		{
			cpn::RigidBody fresh;
			fresh.motion_ = physics::MotionType::Dynamic;
			fresh.layer_ = physics::Layer::Dynamic;

			reg.replace<cpn::RigidBody>(ball, fresh);
			phys.step(0.01f);

			check(phys.bodyCount() == baselineBodies + 4, "replacing a RigidBody left its old body in the world");
			check(physics::hasBody(reg.get<cpn::RigidBody>(ball).bodyId_), "replacing a RigidBody did not rebuild its body");
		}

		reg.destroy(ground);
		reg.destroy(ball);
		reg.destroy(wall);
		reg.destroy(blocker);

		check(phys.bodyCount() == baselineBodies, "destroying entities did not destroy their bodies");

		std::cout << (failures == 0
			? "[physics-check] all checks passed\n"
			: "[physics-check] SOME CHECKS FAILED\n");
	}
}
