/*************************************************************************************************************
\file       input_def.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Exists only to hold the static_asserts that pin input_def.hpp to GLFW's own numbering.
            The header is deliberately GLFW-free so the device banks can be sized without dragging
            GLFW into every consumer, which means nothing in the header itself can catch a drift.
            These do.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "input_def.hpp"

// GLFW
#include <GLFW/glfw3.h>


namespace input::keys
{
	// -------------------------- Array sizes ---------------------------

	static_assert(kMaxKeys == GLFW_KEY_LAST + 1,
		"kMaxKeys must cover every GLFW key code");
	static_assert(kMaxMouseButtons == GLFW_MOUSE_BUTTON_LAST + 1,
		"kMaxMouseButtons must cover every GLFW mouse button");
	static_assert(kMaxPadAxes == GLFW_GAMEPAD_AXIS_LAST + 1,
		"kMaxPadAxes must cover every GLFW gamepad axis");
	static_assert(kMaxGamepads == GLFW_JOYSTICK_LAST + 1,
		"kMaxGamepads must cover every GLFW joystick slot");

	// The two virtual trigger buttons sit immediately above GLFW's real ones.
	static_assert(kMaxPadButtons == GLFW_GAMEPAD_BUTTON_LAST + 1 + 2,
		"kMaxPadButtons must be GLFW's buttons plus the two virtual triggers");

	// --------------------------- Modifiers ----------------------------
	//
	// Mod is used directly as GLFW's mods bitmask rather than translated, so
	// the bit positions have to match exactly.

	static_assert(static_cast<int>(Mod::Shift)		== GLFW_MOD_SHIFT);
	static_assert(static_cast<int>(Mod::Control)	== GLFW_MOD_CONTROL);
	static_assert(static_cast<int>(Mod::Alt)		== GLFW_MOD_ALT);
	static_assert(static_cast<int>(Mod::Super)		== GLFW_MOD_SUPER);
	static_assert(static_cast<int>(Mod::CapsLock)	== GLFW_MOD_CAPS_LOCK);
	static_assert(static_cast<int>(Mod::NumLock)	== GLFW_MOD_NUM_LOCK);

	// ----------------------------- Keys -------------------------------

	static_assert(static_cast<int>(KeyCode::W)			== GLFW_KEY_W);
	static_assert(static_cast<int>(KeyCode::Space)		== GLFW_KEY_SPACE);
	static_assert(static_cast<int>(KeyCode::Escape)		== GLFW_KEY_ESCAPE);
	static_assert(static_cast<int>(KeyCode::L_Shift)	== GLFW_KEY_LEFT_SHIFT);
	static_assert(static_cast<int>(KeyCode::F12)		== GLFW_KEY_F12);
	static_assert(static_cast<int>(KeyCode::Arrow_Up)	== GLFW_KEY_UP);

	// -------------------------- Mouse buttons -------------------------

	static_assert(static_cast<int>(MouseCode::Left_Mouse)	== GLFW_MOUSE_BUTTON_LEFT);
	static_assert(static_cast<int>(MouseCode::Right_Mouse)	== GLFW_MOUSE_BUTTON_RIGHT);
	static_assert(static_cast<int>(MouseCode::Middle_Mouse)	== GLFW_MOUSE_BUTTON_MIDDLE);

	// ------------------------- Gamepad buttons ------------------------
	//
	// The positional names have to land on the labelled GLFW constants, or
	// every pad binding in the default table is silently off by one.

	static_assert(static_cast<int>(PadButton::South)			== GLFW_GAMEPAD_BUTTON_A);
	static_assert(static_cast<int>(PadButton::East)				== GLFW_GAMEPAD_BUTTON_B);
	static_assert(static_cast<int>(PadButton::West)				== GLFW_GAMEPAD_BUTTON_X);
	static_assert(static_cast<int>(PadButton::North)			== GLFW_GAMEPAD_BUTTON_Y);
	static_assert(static_cast<int>(PadButton::LeftBumper)		== GLFW_GAMEPAD_BUTTON_LEFT_BUMPER);
	static_assert(static_cast<int>(PadButton::RightBumper)		== GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER);
	static_assert(static_cast<int>(PadButton::Back)				== GLFW_GAMEPAD_BUTTON_BACK);
	static_assert(static_cast<int>(PadButton::Start)			== GLFW_GAMEPAD_BUTTON_START);
	static_assert(static_cast<int>(PadButton::Guide)			== GLFW_GAMEPAD_BUTTON_GUIDE);
	static_assert(static_cast<int>(PadButton::LeftStickPress)	== GLFW_GAMEPAD_BUTTON_LEFT_THUMB);
	static_assert(static_cast<int>(PadButton::RightStickPress)	== GLFW_GAMEPAD_BUTTON_RIGHT_THUMB);
	static_assert(static_cast<int>(PadButton::DPadUp)			== GLFW_GAMEPAD_BUTTON_DPAD_UP);
	static_assert(static_cast<int>(PadButton::DPadRight)		== GLFW_GAMEPAD_BUTTON_DPAD_RIGHT);
	static_assert(static_cast<int>(PadButton::DPadDown)			== GLFW_GAMEPAD_BUTTON_DPAD_DOWN);
	static_assert(static_cast<int>(PadButton::DPadLeft)			== GLFW_GAMEPAD_BUTTON_DPAD_LEFT);

	// The virtual triggers must not collide with a real button.
	static_assert(static_cast<int>(PadButton::LeftTrigger)	> GLFW_GAMEPAD_BUTTON_LAST);
	static_assert(static_cast<int>(PadButton::RightTrigger)	> GLFW_GAMEPAD_BUTTON_LAST);

	// --------------------------- Gamepad axes -------------------------

	static_assert(static_cast<int>(PadAxis::LeftX)				== GLFW_GAMEPAD_AXIS_LEFT_X);
	static_assert(static_cast<int>(PadAxis::LeftY)				== GLFW_GAMEPAD_AXIS_LEFT_Y);
	static_assert(static_cast<int>(PadAxis::RightX)				== GLFW_GAMEPAD_AXIS_RIGHT_X);
	static_assert(static_cast<int>(PadAxis::RightY)				== GLFW_GAMEPAD_AXIS_RIGHT_Y);
	static_assert(static_cast<int>(PadAxis::LeftTriggerAxis)	== GLFW_GAMEPAD_AXIS_LEFT_TRIGGER);
	static_assert(static_cast<int>(PadAxis::RightTriggerAxis)	== GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER);
}
