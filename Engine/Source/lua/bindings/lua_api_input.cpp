/*************************************************************************************************************
\file       lua_api_input.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Lua Key, Mouse, Action, Pad and PadAxis tables and the Input table: event callbacks (keys,
            text, mouse, actions, gamepads, rebinds), cursor capture, rebinding and action contexts.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "lua_api.hpp"

// Lua
#include "../interface/lua_interface.hpp"
#include "lua_input_events.hpp"
#include "lua_api_util.hpp"

// Input keys and actions
#include "../../input/sys/input_sys.hpp"
#include "../../input/definitions/input_def.hpp"
#include "../../input/definitions/action_list.hpp"

// stds
#include <iostream>
#include <string>
#include <utility>


namespace scripting::lua
{
	/*!
		@brief
			Registers the input code tables and the callback-only Input table.

			Every Input.On* call subscribes a Lua function on the bus through the bridge and
			returns a handle for Input.Off.
		@param lua
			Lua interface to register into
		@param bridge
			Owner of the Lua input subscriptions
		@param inputSys
			The input system, for cursor, gamepad status, rebinding and contexts
		*/
	void registerInputApi(Interface& lua, InputEventBridge& bridge, input::System& inputSys)
	{
		// ------------------ Input API ------------------

		// Key

		sol::table keyTable = lua.createTable("Key");
#define X(name, key) keyTable[#name] = input::keys::KeyCode::name;
		VOX_KEY_LIST
#undef X

		// Input is event-driven. Every Input.On* call subscribes a Lua
		// function on the bus and returns a handle for Input.Off

		namespace ev = input::events;

		// Mouse

		sol::table mouseTable = lua.createTable("Mouse");
#define X(name, button) mouseTable[#name] = input::keys::MouseCode::name;
		VOX_MOUSE_LIST
#undef X

		// Off
		lua.link(
			"Input",
			"Off",
			[&bridge](int handle)
			{
				bridge.remove(handle);
			});

		// OnKey(key, "Pressed" | "Released" | "Repeat", fn(mods))
		lua.link(
			"Input",
			"OnKey",
			[&bridge](int key, std::string phase, sol::protected_function fn)
			{
				if (phase == "Pressed" || phase == "Repeat")
				{
					const bool repeat = (phase == "Repeat");

					return bridge.subscribe<ev::KeyPressed>(
						[key, repeat, fn](const ev::KeyPressed& e)
						{
							if (e.key_ == key && e.repeat_ == repeat)
								InputEventBridge::call(fn, e.mods_);
						});
				}

				if (phase == "Released")
				{
					return bridge.subscribe<ev::KeyReleased>(
						[key, fn](const ev::KeyReleased& e)
						{
							if (e.key_ == key)
								InputEventBridge::call(fn, e.mods_);
						});
				}

				return badPhase("OnKey", phase);
			});

		// OnText(fn(codepoint))
		lua.link(
			"Input",
			"OnText",
			[&bridge](sol::protected_function fn)
			{
				return bridge.subscribe<ev::TextEntered>(
					[fn](const ev::TextEntered& e)
					{
						InputEventBridge::call(fn, e.codepoint_);
					});
			});

		// OnMouseButton(button, "Pressed" | "Released", fn(x, y))
		lua.link(
			"Input",
			"OnMouseButton",
			[&bridge](int button, std::string phase, sol::protected_function fn)
			{
				if (phase == "Pressed")
				{
					return bridge.subscribe<ev::MouseButtonPressed>(
						[button, fn](const ev::MouseButtonPressed& e)
						{
							if (e.button_ == button)
								InputEventBridge::call(fn, e.position_.x, e.position_.y);
						});
				}

				if (phase == "Released")
				{
					return bridge.subscribe<ev::MouseButtonReleased>(
						[button, fn](const ev::MouseButtonReleased& e)
						{
							if (e.button_ == button)
								InputEventBridge::call(fn, e.position_.x, e.position_.y);
						});
				}

				return badPhase("OnMouseButton", phase);
			});

		// OnMouseMove(fn(x, y, dx, dy)) - once per OS sample, not per frame
		lua.link(
			"Input",
			"OnMouseMove",
			[&bridge](sol::protected_function fn)
			{
				return bridge.subscribe<ev::MouseMoved>(
					[fn](const ev::MouseMoved& e)
					{
						InputEventBridge::call(fn,
							e.position_.x, e.position_.y, e.delta_.x, e.delta_.y);
					});
			});

		// OnScroll(fn(dx, dy))
		lua.link(
			"Input",
			"OnScroll",
			[&bridge](sol::protected_function fn)
			{
				return bridge.subscribe<ev::MouseScrolled>(
					[fn](const ev::MouseScrolled& e)
					{
						InputEventBridge::call(fn, e.offset_.x, e.offset_.y);
					});
			});

		lua.link(
			"Input",
			"SetCursorCaptured",
			[&inputSys](bool captured)
			{
				inputSys.setCursorCaptured(captured);
			});

		// ------------------ Action API -----------------
		//
		// This is the API scripts should use. Input.OnKey(Key.W, ...) above
		// hardcodes a keybinding at the call site and gives up gamepad
		// support; Input.OnAction(Action.Move, ...) works on whatever the
		// player has plugged in and survives a rebind.

		sol::table actionTable = lua.createTable("Action");
#define X(name, valueType, ctx) actionTable[#name] = static_cast<int>(input::action::name);
		VOX_ACTION_LIST
#undef X

		sol::table padTable = lua.createTable("Pad");
#define X(name, value) padTable[#name] = static_cast<int>(input::keys::PadButton::name);
		VOX_GAMEPAD_BUTTON_LIST
#undef X

		sol::table padAxisTable = lua.createTable("PadAxis");
#define X(name, value) padAxisTable[#name] = static_cast<int>(input::keys::PadAxis::name);
		VOX_GAMEPAD_AXIS_LIST
#undef X

		// OnAction(action, "Started" | "Performed" | "Canceled" | "Value", fn)
		//
		//   Started, Canceled	fn()
		//   Performed			fn(value)	- Button actions only
		//   Value				fn(x [, y [, z]])	- analog actions, every frame
		//						while actuated; no call means zero
		const auto onAction = [&bridge](input::action::Action a, const std::string& phase,
										sol::protected_function fn)
		{
			if (a == input::action::None || a >= input::action::Count)
			{
				std::cerr << "[lua] Input.OnAction: unknown action\n";
				return 0;
			}

			if (phase == "Started")
			{
				return bridge.subscribe<ev::ActionStarted>(
					[a, fn](const ev::ActionStarted& e)
					{
						if (e.action_ == a)
							InputEventBridge::call(fn);
					});
			}

			if (phase == "Performed")
			{
				return bridge.subscribe<ev::ActionPerformed>(
					[a, fn](const ev::ActionPerformed& e)
					{
						if (e.action_ == a)
							InputEventBridge::call(fn, e.value_);
					});
			}

			if (phase == "Canceled")
			{
				return bridge.subscribe<ev::ActionCanceled>(
					[a, fn](const ev::ActionCanceled& e)
					{
						if (e.action_ == a)
							InputEventBridge::call(fn);
					});
			}

			if (phase == "Value")
			{
				return bridge.subscribe<ev::ActionValue>(
					[a, fn](const ev::ActionValue& e)
					{
						if (e.action_ != a)
							return;

						switch (input::action::componentCount(a))
						{
						case 3:  InputEventBridge::call(fn, e.value_.x, e.value_.y, e.value_.z); break;
						case 2:  InputEventBridge::call(fn, e.value_.x, e.value_.y); break;
						default: InputEventBridge::call(fn, e.value_.x); break;
						}
					});
			}

			return badPhase("OnAction", phase);
		};

		lua.linkOverload(
			"Input",
			"OnAction",
			[onAction](int a, std::string phase, sol::protected_function fn)
			{
				return onAction(static_cast<input::action::Action>(a), phase, std::move(fn));
			},
			[onAction](std::string n, std::string phase, sol::protected_function fn)
			{
				return onAction(input::action::fromName(n), phase, std::move(fn));
			});

		// OnRebind(fn(action, canceled))
		lua.link(
			"Input",
			"OnRebind",
			[&bridge](sol::protected_function fn)
			{
				return bridge.subscribe<ev::RebindCompleted>(
					[fn](const ev::RebindCompleted& e)
					{
						InputEventBridge::call(fn, static_cast<int>(e.action_), e.canceled_);
					});
			});

		lua.link(
			"Input",
			"ActionName",
			[](int a)
			{
				return std::string(input::action::name(
					static_cast<input::action::Action>(a)));
			});

		lua.link(
			"Input",
			"ActionFromName",
			[](std::string n)
			{
				return static_cast<int>(input::action::fromName(n));
			});

		// ------------------ Gamepad API ----------------

		lua.link(
			"Input",
			"GamepadConnected",
			[&inputSys]()
			{
				return inputSys.primaryPad() >= 0;
			});

		lua.link(
			"Input",
			"GamepadName",
			[&inputSys]()
			{
				return inputSys.padName(inputSys.primaryPad());
			});

		// OnGamepad("Connected" | "Disconnected" | "PrimaryChanged", fn(slot, name))
		lua.link(
			"Input",
			"OnGamepad",
			[&bridge, &inputSys](std::string phase, sol::protected_function fn)
			{
				if (phase == "Connected")
				{
					return bridge.subscribe<ev::GamepadConnected>(
						[fn](const ev::GamepadConnected& e)
						{
							InputEventBridge::call(fn, e.slot_, e.name_);
						});
				}

				if (phase == "Disconnected")
				{
					return bridge.subscribe<ev::GamepadDisconnected>(
						[fn](const ev::GamepadDisconnected& e)
						{
							InputEventBridge::call(fn, e.slot_);
						});
				}

				if (phase == "PrimaryChanged")
				{
					return bridge.subscribe<ev::PrimaryGamepadChanged>(
						[&inputSys, fn](const ev::PrimaryGamepadChanged& e)
						{
							InputEventBridge::call(fn, e.slot_, inputSys.padName(e.slot_));
						});
				}

				return badPhase("OnGamepad", phase);
			});

		// OnGamepadButton(button, "Pressed" | "Released", fn(slot))
		lua.link(
			"Input",
			"OnGamepadButton",
			[&bridge](int button, std::string phase, sol::protected_function fn)
			{
				if (phase == "Pressed")
				{
					return bridge.subscribe<ev::GamepadButtonPressed>(
						[button, fn](const ev::GamepadButtonPressed& e)
						{
							if (e.button_ == button)
								InputEventBridge::call(fn, e.slot_);
						});
				}

				if (phase == "Released")
				{
					return bridge.subscribe<ev::GamepadButtonReleased>(
						[button, fn](const ev::GamepadButtonReleased& e)
						{
							if (e.button_ == button)
								InputEventBridge::call(fn, e.slot_);
						});
				}

				return badPhase("OnGamepadButton", phase);
			});

		// ------------------ Rebinding ------------------

		lua.link(
			"Input",
			"BindKey",
			[&inputSys](int a, std::string keyName)
			{
				const auto& km = input::keys::KeyMap();
				const auto it = km.find(keyName);

				if (it == km.end())
					return false;

				const auto action = static_cast<input::action::Action>(a);
				const auto ctx = input::action::kDefaultContext[action];

				inputSys.clearBindings(ctx, action);
				inputSys.addBinding(ctx, input::action::btn(
					action, input::action::Device::Keyboard, it->second));

				return true;
			});

		lua.link(
			"Input",
			"AddBindKey",
			[&inputSys](int a, std::string keyName)
			{
				const auto& km = input::keys::KeyMap();
				const auto it = km.find(keyName);

				if (it == km.end())
					return false;

				const auto action = static_cast<input::action::Action>(a);

				inputSys.addBinding(input::action::kDefaultContext[action],
					input::action::btn(action,
						input::action::Device::Keyboard, it->second));

				return true;
			});

		lua.link(
			"Input",
			"BindPad",
			[&inputSys](int a, int padButton)
			{
				const auto action = static_cast<input::action::Action>(a);
				const auto ctx = input::action::kDefaultContext[action];

				inputSys.clearBindings(ctx, action);
				inputSys.addBinding(ctx, input::action::btn(
					action, input::action::Device::Gamepad, padButton));
			});

		lua.link(
			"Input",
			"ClearBindings",
			[&inputSys](int a)
			{
				const auto action = static_cast<input::action::Action>(a);

				inputSys.clearBindings(input::action::kDefaultContext[action], action);
			});

		lua.link(
			"Input",
			"ResetBindings",
			[&inputSys]()
			{
				inputSys.resetBindingsToDefaults();
			});

		lua.link(
			"Input",
			"StartRebind",
			[&inputSys](int a)
			{
				const auto action = static_cast<input::action::Action>(a);

				inputSys.startInteractiveRebind(
					input::action::kDefaultContext[action], action);
			});

		lua.link(
			"Input",
			"CancelRebind",
			[&inputSys]()
			{
				inputSys.cancelInteractiveRebind();
			});

		lua.link(
			"Input",
			"RebindInProgress",
			[&inputSys]()
			{
				return inputSys.rebindInProgress();
			});

		// ------------------ Contexts -------------------

		lua.link(
			"Input",
			"PushContext",
			[&inputSys](std::string name)
			{
				if (name == "UI")				inputSys.pushContext(input::action::Context::UI);
				else if (name == "Editor")		inputSys.pushContext(input::action::Context::Editor);
				else if (name == "Camera")		inputSys.pushContext(input::action::Context::Camera);
				else if (name == "Gameplay")	inputSys.pushContext(input::action::Context::Gameplay);
			});

		lua.link(
			"Input",
			"PopContext",
			[&inputSys](std::string name)
			{
				if (name == "UI")				inputSys.popContext(input::action::Context::UI);
				else if (name == "Editor")		inputSys.popContext(input::action::Context::Editor);
				else if (name == "Camera")		inputSys.popContext(input::action::Context::Camera);
				else if (name == "Gameplay")	inputSys.popContext(input::action::Context::Gameplay);
			});
	}
}
