/*************************************************************************************************************
\file       input_events.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Every input event input::System publishes on the bus: frame start, keyboard, text, mouse,
            gamepad, focus, action edges and values, and rebind completion.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Action ids
#include "../definitions/action_list.hpp"

// action::Device
#include "../action/binding.hpp"

// GLM
#include <glm/glm.hpp>

// stds
#include <string>


// ---------------------------------------------------------------------------
//  Input events.
//
//  The bus is the ONLY way input reaches a consumer. input::System has no
//  down/pressed/value queries; anything that needs input subscribes.
//
//  One frame publishes, in order:
//   1. FrameStarted - zero per-frame accumulators here.
//   2. Device events (keys, mouse, pads, focus) in the order GLFW produced
//      them.
//   3. Action events: Started/Performed/Canceled on edges, then ActionValue
//      for every analog action that is actuated THIS frame.
//
//  Continuous values arrive as ActionValue, once per frame per actuated
//  action. An analog action that sends no ActionValue this frame is at zero,
//  which is why FrameStarted exists. Held state for a button is not
//  re-published - track it from Started / Canceled.
//
//  The bus has no replay. A subscriber registered while a key is held never
//  sees that key's press, so it starts from "released".
//
//  Every one of these is published from input::System::update(), on the main
//  thread, never from inside a GLFW callback. Handlers must stay cheap.
// ---------------------------------------------------------------------------

namespace input::events
{
	// ----------------------------- Frame ----------------------------

	/**
	 * A new input frame is starting.
	 *
	 * Published before any other input event of the frame. Consumers that
	 * accumulate ActionValue reset here, so a value that stops arriving reads
	 * as zero rather than sticking at its last reading.
	 *
	 */
	struct FrameStarted
	{
	};

	// --------------------------- Keyboard ---------------------------

	struct KeyPressed
	{
		int		key_		{ 0 };	// input::keys::KeyCode
		int		scancode_	{ 0 };	// platform scancode, for physical-position binding
		int		mods_		{ 0 };	// input::keys::Mod bitmask
		bool	repeat_		{ false };	// auto-repeat, not a fresh press
	};

	struct KeyReleased
	{
		int		key_		{ 0 };
		int		scancode_	{ 0 };
		int		mods_		{ 0 };
	};

	/**
	 * A translated unicode codepoint.
	 *
	 * Separate from KeyPressed because a key is a physical location and a
	 * character is what the OS layout, dead keys and IME produced - they are
	 * not the same thing. KeyPressed(Num2, Shift) is " on a UK layout, @ on a
	 * US one and neither on AZERTY, so a text field cannot reconstruct this
	 * after the fact. Never routed through the action layer.
	 *
	 */
	struct TextEntered
	{
		unsigned int codepoint_ { 0 };
	};

	// ----------------------------- Mouse ----------------------------

	struct MouseButtonPressed
	{
		int			button_		{ 0 };	// input::keys::MouseCode
		int			mods_		{ 0 };
		glm::vec2	position_	{ 0.f };	// window pixels, origin top-left
	};

	struct MouseButtonReleased
	{
		int			button_		{ 0 };
		int			mods_		{ 0 };
		glm::vec2	position_	{ 0.f };
	};

	/**
	 * Cursor movement.
	 *
	 * Published once per GLFW sample, so a fast mouse produces several per
	 * frame. Anything that only needs "how far did the cursor move this
	 * frame" should subscribe to ActionValue for LookMouse, which carries the
	 * frame's accumulated delta, instead of summing these.
	 *
	 */
	struct MouseMoved
	{
		glm::vec2	position_	{ 0.f };
		glm::vec2	delta_		{ 0.f };	// +y is the cursor moving DOWN
	};

	struct MouseScrolled
	{
		glm::vec2	offset_		{ 0.f };	// wheel ticks, y is the usual wheel
	};

	struct CursorModeChanged
	{
		int		mode_		{ 0 };	// GLFW_CURSOR_NORMAL / HIDDEN / DISABLED
		bool	captured_	{ false };
	};

	// ---------------------------- Gamepad ---------------------------

	struct GamepadConnected
	{
		int			slot_	{ -1 };
		std::string	name_	{};
		bool		mapped_	{ false };	// false = present but GLFW has no gamepad mapping
	};

	struct GamepadDisconnected
	{
		int slot_ { -1 };
	};

	struct GamepadButtonPressed
	{
		int slot_	{ -1 };
		int button_	{ 0 };	// input::keys::PadButton
	};

	struct GamepadButtonReleased
	{
		int slot_	{ -1 };
		int button_	{ 0 };
	};

	/**
	 * The pad driving "the player" changed.
	 *
	 * Published whenever the lowest connected mapped gamepad changes,
	 * including to and from none (slot_ == -1). This is the hook for swapping
	 * on-screen button glyphs.
	 *
	 */
	struct PrimaryGamepadChanged
	{
		int previousSlot_	{ -1 };
		int slot_			{ -1 };
	};

	// ----------------------------- Focus ----------------------------

	struct WindowFocusChanged
	{
		bool focused_ { false };
	};

	// ---------------------------- Actions ---------------------------

	struct ActionStarted
	{
		action::Action	action_		{ action::None };
		action::Context	context_	{ action::Context::Gameplay };
		action::Device	device_		{ action::Device::None };	// device that actuated it
	};

	struct ActionPerformed
	{
		action::Action	action_		{ action::None };
		action::Context	context_	{ action::Context::Gameplay };
		float			value_		{ 0.f };	// 1.0 for a button, magnitude for an axis
	};

	/**
	 * An action stopped being actuated.
	 *
	 * Also published when the device driving it disappears mid-actuation - a
	 * pad unplug or a focus loss - so a subscriber that started a charge-up on
	 * ActionStarted is always told to stop.
	 *
	 */
	struct ActionCanceled
	{
		action::Action	action_		{ action::None };
		action::Context	context_	{ action::Context::Gameplay };
	};

	/**
	 * This frame's value of an analog action.
	 *
	 * Published every frame for each Axis1 / Axis2 / Axis3 action while it is
	 * actuated, after the edge events. Never published for Button actions.
	 * Absence this frame means the value is zero - reset on FrameStarted.
	 *
	 * Units follow the action: LookMouse and ZoomTicks are frame-absolute and
	 * must NOT be scaled by dt; LookStick, ZoomRate and Move are rates or
	 * intents the consumer scales. Composites built from buttons are not
	 * normalized.
	 *
	 */
	struct ActionValue
	{
		action::Action	action_		{ action::None };
		action::Context	context_	{ action::Context::Gameplay };
		glm::vec3		value_		{ 0.f };	// components past componentCount() are 0
		action::Device	device_		{ action::Device::None };
	};

	/**
	 * Interactive rebinding finished.
	 *
	 * Published from the frame drain, never from a GLFW callback, so a handler
	 * may immediately arm another rebind.
	 *
	 */
	struct RebindCompleted
	{
		action::Action	action_		{ action::None };
		bool			canceled_	{ false };
	};
}
