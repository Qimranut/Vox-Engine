/*************************************************************************************************************
\file       event_wiring.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Implements the cross-subsystem bus subscriptions: camera and character intent from input,
            the camera-mode toggle, the orbit camera following the player, the voxel stream focus
            following the camera, and the spring arm's physics sphere cast.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "event_wiring.hpp"

// Engine
#include "../systems/system_manager.hpp"

// Subsystems
#include "../../events/bus.hpp"
#include "../../input/sys/input_sys.hpp"
#include "../../input/definitions/action_list.hpp"
#include "../../graphics/sys/gph_sys.hpp"
#include "../../physics/sys/phy_sys.hpp"
#include "../../voxel/sys/voxel_sys.hpp"

// Events published across subsystems
#include "../../physics/events/physics_events.hpp"
#include "../../graphics/events/graphics_events.hpp"

// GLM
#include <glm/glm.hpp>

// stds
#include <iostream>


namespace engine::wiring
{
	/*!
		@brief
			Keeps camera intent up to date from input events.

			This is the only layer that knows both the input events and the camera
			intent struct. Analog channels reset on FrameStarted; look and sprint
			flags follow Started / Canceled.
		@param bus
			Bus to subscribe on
		@param out
			Intent to fill; must outlive the subscriptions
		*/
	void subscribeCameraIntent(events::Bus& bus, graphics::camera::ControllerInput& out)
	{
		using namespace input::action;
		namespace ev = input::events;

		// Zero only the analog channels. Forgetting this leaves the last
		// mouse delta applied again on every frame after the mouse stops.
		bus.subscribe<ev::FrameStarted>(
			[&out](const ev::FrameStarted&)
			{
				out.lookDelta_		= glm::vec2{ 0.f };
				out.lookStick_		= glm::vec2{ 0.f };
				out.moveAxis_		= glm::vec3{ 0.f };
				out.scrollDelta_	= 0.f;
				out.zoomRate_		= 0.f;
			});

		bus.subscribe<ev::ActionValue>(
			[&out](const ev::ActionValue& e)
			{
				switch (e.action_)
				{
				case Move:		out.moveAxis_		= e.value_;				break;
				case LookMouse:	out.lookDelta_		= glm::vec2(e.value_);	break;
				case LookStick:	out.lookStick_		= glm::vec2(e.value_);	break;
				case ZoomTicks:	out.scrollDelta_	= e.value_.x;			break;
				case ZoomRate:	out.zoomRate_		= e.value_.x;			break;
				default:												break;
				}
			});

		// Mouse look is gated on the look button; stick look is always
		// live, because a pad has no cursor to fight with.
		const auto setFlag = [&out](Action a, bool held)
		{
			if (a == LookModifier)
				out.lookActive_ = held;
			else if (a == Sprint)
				out.sprint_ = held;
		};

		bus.subscribe<ev::ActionStarted>(
			[setFlag](const ev::ActionStarted& e) { setFlag(e.action_, true); });

		bus.subscribe<ev::ActionCanceled>(
			[setFlag](const ev::ActionCanceled& e) { setFlag(e.action_, false); });
	}

	/*!
		@brief
			Keeps the player character's intent up to date from input and camera
			events: move, sprint, the latched jump and the camera heading.
		@param bus
			Bus to subscribe on
		@param out
			Intent to fill; must outlive the subscriptions
		*/
	void subscribeCharacterIntent(events::Bus& bus, physics::character::Intent& out)
	{
		using namespace input::action;
		namespace ev = input::events;

		// Move is analog: no ActionValue this frame means zero.
		bus.subscribe<ev::FrameStarted>(
			[&out](const ev::FrameStarted&)
			{
				out.move_ = glm::vec2{ 0.f };
			});

		// Move is x = right, y = up, z = forward. The character walks on
		// the ground plane, so up is dropped.
		bus.subscribe<ev::ActionValue>(
			[&out](const ev::ActionValue& e)
			{
				if (e.action_ == Move)
					out.move_ = glm::vec2{ e.value_.x, e.value_.z };
			});

		bus.subscribe<ev::ActionStarted>(
			[&out](const ev::ActionStarted& e)
			{
				if (e.action_ == Sprint)
					out.sprint_ = true;
			});

		bus.subscribe<ev::ActionCanceled>(
			[&out](const ev::ActionCanceled& e)
			{
				if (e.action_ == Sprint)
					out.sprint_ = false;
			});

		// An EDGE. It is latched rather than applied, because the fixed
		// loop may run zero times this frame or several - the first
		// substep that can jump consumes it.
		bus.subscribe<ev::ActionPerformed>(
			[&out](const ev::ActionPerformed& e)
			{
				if (e.action_ != Jump)
					return;

				out.jumpPending_ = true;
				out.jumpBuffer_ = physics::character::kJumpBufferSeconds;
			});

		bus.subscribe<graphics::events::CameraUpdated>(
			[&out](const graphics::events::CameraUpdated& e)
			{
				out.headingYaw_ = e.yaw_;
			});
	}

	/*!
		@brief
			Makes every engine-level bus subscription between subsystems.

			Must run before the window is created: the bus has no replay.
		@param systems
			Constructed, not yet initialised, subsystems
		*/
	void connectEvents(SystemManager& systems)
	{
		events::Bus& bus = systems.bus();
		input::System& in = systems.inputSys();
		graphics::System& gph = systems.graphicsSys();
		physics::System& phys = systems.physicsSys();

		in.subscriptionSetup();

		// Right mouse button holds the look drag, and only the mouse may
		// capture the cursor - a pad has no pointer to hide.
		in.setCaptureWhileHeld(input::action::LookModifier);

		subscribeCameraIntent(bus, gph.cameraInput());

		// Performed fires once per physical press, so the toggle cannot
		// doublefire however many times the frame is looked at.
		bus.subscribe<input::events::ActionPerformed>(
			[&gph](const input::events::ActionPerformed& e)
			{
				if (e.action_ != input::action::ToggleCameraMode)
					return;

				gph.setCameraMode(gph.cameraMode() == graphics::CameraMode::Fly
					? graphics::CameraMode::Orbit
					: graphics::CameraMode::Fly);
			});

		// ---------------------------- Physics ----------------------------

		subscribeCharacterIntent(bus, phys.characterIntent());

		// The camera follows the player through the bus: physics has no idea
		// a camera exists, and graphics has no idea physics does.
		bus.subscribe<physics::events::CharacterPoseUpdated>(
			[&gph](const physics::events::CharacterPoseUpdated& e)
			{
				if (e.playerControlled_)
					gph.orbit().setTarget(e.feet_);
			});

		// ----------------------------- Voxel -----------------------------

		// A streamed terrain is generated round the camera. Following the
		// player, the camera is a spring arm's length from them, which is
		// nothing next to the rings; flying free, or in the Editor, it is the
		// only thing there is to follow. The event comes after the voxel
		// system's update in a frame, so streaming works from last frame's
		// position - metres matter to it, not centimetres.
		voxel::System& vox = systems.voxelSys();

		bus.subscribe<graphics::events::CameraUpdated>(
			[&vox](const graphics::events::CameraUpdated& e)
			{
				vox.setStreamFocus(e.position_);
			});
	}

	/*!
		@brief
			Gives the orbit camera's spring arm a physics sphere cast that uses the
			Camera query filter.
		@param systems
			Subsystems with physics initialised
		*/
	void linkCameraCollision(SystemManager& systems)
	{
		physics::System& phys = systems.physicsSys();

		// The spring arm. A query, not an event: the camera needs the answer
		// before it can place itself this frame, and the bus is one-way.
		// The Camera filter ignores the player's own body and debris.
		systems.graphicsSys().orbit().setCollisionProbe(
			[&phys](const glm::vec3& origin, const glm::vec3& direction, float maxDistance, float radius)
			{
				const auto hit = phys.sphereCast(
					origin, direction, maxDistance, radius, physics::QueryFilter::Camera);

				return hit ? hit->distance_ : maxDistance;
			});
	}
}
