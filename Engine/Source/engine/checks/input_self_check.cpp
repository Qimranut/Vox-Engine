/*************************************************************************************************************
\file       input_self_check.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Startup self-check for input: radial deadzone, trigger remapping, look sign and units,
            camera intent wiring, UI suppression and the default binding table.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "input_self_check.hpp"

// Engine
#include "../wiring/event_wiring.hpp"

// Subsystems
#include "../../events/bus.hpp"
#include "../../input/sys/input_sys.hpp"
#include "../../input/definitions/input_def.hpp"
#include "../../input/definitions/action_list.hpp"
#include "../../graphics/sys/gph_sys.hpp"

// GLM
#include <glm/glm.hpp>

// stds
#include <cmath>
#include <iostream>


namespace engine::checks
{
	/*!
		@brief
			Verifies the input system's pure maths and wiring at startup and prints
			"[input-check]" results to stdout / stderr. Never throws.
		*/
	void runInputSelfCheck()
	{
		using namespace input::action;
		using input::device::triggerToUnit;

		int failures = 0;

		const auto check = [&failures](bool ok, const char* what)
		{
			if (!ok)
			{
				std::cerr << "[input-check] FAILED: " << what << "\n";
				++failures;
			}
		};

		const auto nearly = [](float a, float b, float eps)
		{
			return std::fabs(a - b) < eps;
		};

		// ----------------------- Deadzone -----------------------

		check(glm::length(applyRadialDeadzone({ 0.1f, 0.1f }, 0.2f, 0.95f)) == 0.f,
			"stick centre is not dead");

		// Just outside the zone must be NEAR zero. A deadzone that clamps
		// instead of rescaling jumps straight to 0.2 here, and the camera
		// lurches the instant the stick leaves centre.
		check(glm::length(applyRadialDeadzone({ 0.21f, 0.f }, 0.2f, 0.95f)) < 0.02f,
			"deadzone is not rescaled - value jumps at the edge");

		// Full deflection must reach exactly 1, and a DIAGONAL must reach it
		// too. A per-axis deadzone passes the first and fails the second,
		// which is the whole reason the deadzone is radial.
		check(nearly(glm::length(applyRadialDeadzone({ 1.f, 0.f }, 0.2f, 0.95f)), 1.f, 1e-4f),
			"full cardinal deflection does not reach 1");

		check(nearly(glm::length(applyRadialDeadzone(
			glm::normalize(glm::vec2{ 1.f, 1.f }), 0.2f, 0.95f)), 1.f, 1e-4f),
			"full DIAGONAL deflection does not reach 1 - deadzone is square");

		// ----------------------- Triggers -----------------------
		//
		// GLFW rests the triggers at -1. Missing the conversion leaves every
		// trigger-bound action permanently firing.

		check(nearly(triggerToUnit(-1.f), 0.f, 1e-6f), "trigger at rest is not 0");
		check(nearly(triggerToUnit(0.f), 0.5f, 1e-6f), "trigger midpoint is wrong");
		check(nearly(triggerToUnit(1.f), 1.f, 1e-6f), "trigger fully pulled is not 1");

		// --------------------- Look sign ------------------------

		const auto yawAfter = [](glm::vec2 mouse, glm::vec2 stick, float dt)
		{
			graphics::camera::Camera cam;
			graphics::camera::FlyController fly;
			graphics::camera::ControllerInput in;

			cam.setYawPitch(0.f, 0.f);

			in.lookActive_ = true;
			in.lookDelta_ = mouse;
			in.lookStick_ = stick;

			fly.update(cam, in, dt);

			return cam.yaw();
		};

		check(yawAfter({ 100.f, 0.f }, { 0.f, 0.f }, 1.f / 60.f) < 0.f,
			"mouse-right must DECREASE yaw");

		check(yawAfter({ 0.f, 0.f }, { 1.f, 0.f }, 1.f / 60.f) < 0.f,
			"stick-right must DECREASE yaw");

		// ------------------- Look units -------------------------

		check(nearly(yawAfter({ 100.f, 0.f }, { 0.f, 0.f }, 1.f / 30.f),
					 yawAfter({ 100.f, 0.f }, { 0.f, 0.f }, 1.f / 240.f), 1e-6f),
			"mouse look is frame-rate dependent - delta is being scaled by dt");

		check(nearly(yawAfter({ 0.f, 0.f }, { 1.f, 0.f }, 1.f / 30.f),
					 8.f * yawAfter({ 0.f, 0.f }, { 1.f, 0.f }, 1.f / 240.f), 1e-4f),
			"stick look is not scaled by dt - turn rate follows frame rate");

		// ------------------- Camera intent ----------------------
		{
			namespace ev = input::events;

			events::Bus bus;
			graphics::camera::ControllerInput intent;

			wiring::subscribeCameraIntent(bus, intent);

			bus.publish(ev::FrameStarted{});
			bus.publish(ev::ActionStarted{ Sprint, Context::Camera, input::action::Device::Keyboard });
			bus.publish(ev::ActionStarted{ LookModifier, Context::Camera, input::action::Device::Mouse });
			bus.publish(ev::ActionValue{ Move, Context::Camera, { 1.f, 0.f, 0.f }, input::action::Device::Keyboard });
			bus.publish(ev::ActionValue{ LookMouse, Context::Camera, { 5.f, -3.f, 0.f }, input::action::Device::Mouse });
			bus.publish(ev::ActionValue{ ZoomTicks, Context::Camera, { 2.f, 0.f, 0.f }, input::action::Device::Mouse });

			check(intent.moveAxis_ == glm::vec3(1.f, 0.f, 0.f), "ActionValue(Move) did not reach moveAxis_");
			check(intent.lookDelta_ == glm::vec2(5.f, -3.f), "ActionValue(LookMouse) did not reach lookDelta_");
			check(intent.scrollDelta_ == 2.f, "ActionValue(ZoomTicks) did not reach scrollDelta_");
			check(intent.lookActive_, "ActionStarted(LookModifier) did not set lookActive_");
			check(intent.sprint_, "ActionStarted(Sprint) did not set sprint_");

			bus.publish(ev::FrameStarted{});

			check(intent.moveAxis_ == glm::vec3(0.f), "moveAxis_ not reset on FrameStarted");
			check(intent.lookDelta_ == glm::vec2(0.f),
				"lookDelta_ not reset on FrameStarted - a stale mouse delta spins the camera forever");
			check(intent.scrollDelta_ == 0.f, "scrollDelta_ not reset on FrameStarted");
			check(intent.sprint_ && intent.lookActive_, "FrameStarted cleared held button state");

			bus.publish(ev::ActionCanceled{ LookModifier, Context::Camera });

			check(!intent.lookActive_, "ActionCanceled(LookModifier) did not clear lookActive_");
			check(intent.sprint_, "canceling one action cleared another");
		}

		// ------------------- UI suppression --------------------
		//
		// System::setSuppressed hides a device by handing the resolver a null
		// pointer for it. A held key must then read as released, or typing
		// "w" into an editor text box flies the camera.
		{
			input::device::Keyboard keyboard;
			keyboard.onKey(input::keys::W, 1 /* GLFW_PRESS */, 0);

			const Binding forward = btn(Sprint, Device::Keyboard, input::keys::W);

			DeviceView view;
			view.keyboard_ = &keyboard;
			check(evaluate(forward, view).actuated_, "a held key does not actuate its binding");

			view.keyboard_ = nullptr;
			check(!evaluate(forward, view).actuated_, "a suppressed keyboard still actuates its binding");
		}

		// ------------------- Binding table ----------------------

		for (const auto& b : defaultBindings())
		{
			check(b.action_ < Count, "binding names an action that does not exist");
			check(b.innerDeadzone_ < b.outerDeadzone_, "binding has an inverted deadzone band");
		}

		// ------------------- Stick placement --------------------
		//
		// Move is x right, y world up, z forward, and the character walks on
		// (x, z). A stick whose Y lands in y walks the player nowhere.
		{
			bool sawMoveStick = false;

			for (const auto& b : defaultBindings())
			{
				if (b.kind_ != SourceKind::PadStick2)
					continue;

				// Pushed fully up: GLFW reports stick Y as +down.
				const glm::vec3 up = resolveStick(b, { 0.f, -1.f });
				const glm::vec3 right = resolveStick(b, { 1.f, 0.f });

				if (b.action_ == Move)
				{
					sawMoveStick = true;

					check(nearly(up.z, 1.f, 1e-4f) && up.y == 0.f,
						"left stick up does not move FORWARD - its Y must land in Move.z");
					check(nearly(right.x, 1.f, 1e-4f) && right.z == 0.f,
						"left stick right does not strafe right");
				}
				else if (b.action_ == LookStick)
				{
					check(nearly(up.y, -1.f, 1e-4f) && up.z == 0.f,
						"right stick up does not look up (LookStick.y must be negative)");
				}
			}

			check(sawMoveStick, "Move has no default stick binding");
		}

		std::cout << (failures == 0
			? "[input-check] all checks passed\n"
			: "[input-check] SOME CHECKS FAILED\n");
	}
}
