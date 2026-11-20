/*************************************************************************************************************
\file       raw_event.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      RawEvent: one piece of unprocessed device input queued by the GLFW callbacks and drained by
            input::System::update() at one defined point in the frame.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// stds
#include <cstdint>


namespace input
{
	/**
	 * What a RawEvent carries.
	 *
	 */
	enum class RawType : std::uint8_t
	{
		Key,
		Char,
		MouseButton,
		CursorPos,
		Scroll,
		Focus,
		PadButton,
		PadConnect
	};

	/**
	 * One piece of unprocessed device input, as it left the OS.
	 *
	 * GLFW callbacks push these into a queue and input::System::update()
	 * drains it at one defined point in the frame. They deliberately do NOT
	 * publish to the event bus directly:
	 *
	 *  - a bus handler running from inside glfwPollEvents runs at an arbitrary
	 *    stack depth, and a handler that subscribes would resize the very
	 *    vector the dispatch is walking,
	 *  - Windows pumps callbacks from inside the modal window-drag loop, where
	 *    glfwPollEvents itself is blocked, so gameplay handlers would fire
	 *    while the user is moving the window,
	 *  - frame logic order would otherwise depend on how many input messages
	 *    the OS happened to coalesce,
	 *  - the action layer has to resolve against a COMPLETE frame of input.
	 *
	 * A plain struct rather than a variant: trivially copyable, one switch in
	 * the drain, and the field aliasing is documented instead of clever.
	 *
	 */
	struct RawEvent
	{
		RawType	type_	{ RawType::Key };

		int		code_	{ 0 };	// key / mouse button / pad button / joystick slot
		int		scan_	{ 0 };	// platform scancode (Key only)
		int		state_	{ 0 };	// GLFW_PRESS / GLFW_RELEASE / GLFW_REPEAT, or a bool
		int		mods_	{ 0 };	// input::keys::Mod bitmask
		int		device_	{ -1 };	// joystick slot for pad events, -1 otherwise

		double	x_		{ 0.0 };	// cursor x / scroll x / unicode codepoint
		double	y_		{ 0.0 };	// cursor y / scroll y

		/*!
			@brief
				Builds a gamepad button edge.
			@param slot
				Joystick slot the pad occupies
			@param button
				input::keys::PadButton
			@param isDown
				true for a press, false for a release
			@return
				The event, ready to push
			*/
		static RawEvent padButton(int slot, int button, bool isDown)
		{
			RawEvent e;

			e.type_ = RawType::PadButton;
			e.code_ = button;
			e.state_ = isDown ? 1 : 0;
			e.device_ = slot;

			return e;
		}
	};
}
