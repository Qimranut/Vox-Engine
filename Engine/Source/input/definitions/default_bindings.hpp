/*************************************************************************************************************
\file       default_bindings.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      VOX_DEFAULT_BINDING_LIST, the default control bindings (one source per entry), expanded
            into the default binding table in action_map.cpp. See the sign table below before
            changing an invert flag.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once


// ---------------------------------------------------------------------------
//  Default control bindings.
//
//  Each entry is ONE source feeding ONE action. An action appears as many
//  times as it has sources - that is how a control ends up working on keyboard
//  and pad at once. Bindings never sum: the resolver takes the largest
//  magnitude, so a plugged-in pad resting at zero can never add speed to a
//  keyboard input.
//
//  Entry macros, all expanded in action_map.cpp:
//
//    B_BTN    (action, Device, code)
//    B_AXIS1B (action, Device, posCode, negCode)
//    B_AXIS3B (action, Device, +x, -x, +y, -y, +z, -z)
//    B_STICK  (action, axisX, axisY, invertX, invertY)
//    B_PADAX  (action, axis, invert)
//    B_PADTRG (action, axis)
//    B_MDELTA (action)
//    B_SCROLL (action)
//
// ---------------------------------------------------------------------------
//
//  THE SIGN TABLE. Read this before "fixing" an invert flag below.
//
//    Source          Raw convention        Wanted                 Action
//    ------------------------------------------------------------------
//    Mouse X         +right                +x = look right        none
//    Mouse Y         +down (screen)        +y = look down         none
//    Right stick X   +right                +x = look right        none
//    Right stick Y   +DOWN (GLFW)          +y = look down         none
//    Left stick X    +right                +x = strafe right      none
//    Left stick Y    +DOWN (GLFW)          +z = move FORWARD      NEGATE
//
//  That is why LookStick has no Y invert and Move does. It looks inconsistent
//  and it is correct: for LOOK, GLFW's +down already matches the screen-space
//  convention the camera controllers subtract against (see camera.hpp); for
//  MOVE, +down has to become negative-forward. This is the single most likely
//  line in the input system to be "corrected" into a bug.
//
//  A stick bound to a three-component action (Move) drives the ground plane:
//  resolveStick() in binding.hpp places its Y in z, AFTER the invert, so the
//  B_STICK line below writes Move.x and Move.z and never Move.y.
//
// ---------------------------------------------------------------------------

#define VOX_DEFAULT_BINDING_LIST \
	/* ---------------------------- Move ----------------------------- */     \
	B_AXIS3B (Move, Keyboard, keys::D, keys::A, keys::E, keys::Q, keys::W, keys::S) \
	B_STICK  (Move, keys::PadAxis::LeftX, keys::PadAxis::LeftY, false, true)  \
	/* Bumpers drive world-up only. -1 means "no button bound" for that      */\
	/* direction, so this contributes to y and leaves x and z to the stick.  */\
	B_AXIS3B (Move, Gamepad, -1, -1,                                          \
	          keys::PadButton::RightBumper, keys::PadButton::LeftBumper, -1, -1) \
	                                                                          \
	/* --- Look: two SEPARATE actions, because the units differ ------- */     \
	B_MDELTA (LookMouse)                                                      \
	B_STICK  (LookStick, keys::PadAxis::RightX, keys::PadAxis::RightY, false, false) \
	                                                                          \
	/* -------------------------- Modifiers -------------------------- */     \
	B_BTN    (LookModifier, Mouse,    keys::Right_Mouse)                      \
	B_BTN    (Sprint,       Keyboard, keys::L_Shift)                          \
	B_BTN    (Sprint,       Keyboard, keys::R_Shift)                          \
	B_BTN    (Sprint,       Gamepad,  keys::PadButton::LeftStickPress)        \
	                                                                          \
	/* ------------------------- Zoom / speed ------------------------ */     \
	B_SCROLL (ZoomTicks)                                                      \
	B_AXIS1B (ZoomRate, Gamepad, keys::PadButton::DPadUp, keys::PadButton::DPadDown) \
	                                                                          \
	/* --------------------------- Gameplay -------------------------- */     \
	B_BTN    (Fire,     Mouse,    keys::Left_Mouse)                           \
	B_BTN    (Fire,     Gamepad,  keys::PadButton::RightTrigger)              \
	B_BTN    (Jump,     Keyboard, keys::Space)                                \
	B_BTN    (Jump,     Gamepad,  keys::PadButton::South)                     \
	B_BTN    (Interact, Keyboard, keys::F)                                    \
	B_BTN    (Interact, Gamepad,  keys::PadButton::West)                      \
	                                                                          \
	/* ------------------------- Camera / debug ---------------------- */     \
	B_BTN    (ToggleCameraMode, Keyboard, keys::F5)                           \
	B_BTN    (ToggleCameraMode, Gamepad,  keys::PadButton::Back)              \
	B_BTN    (DebugInput,       Keyboard, keys::F3)                           \
	                                                                          \
	/* ------------------------------ UI ----------------------------- */     \
	B_BTN    (UISubmit, Keyboard, keys::Enter)                                \
	B_BTN    (UISubmit, Gamepad,  keys::PadButton::South)                     \
	B_BTN    (UICancel, Keyboard, keys::Escape)                               \
	B_BTN    (UICancel, Gamepad,  keys::PadButton::East)                      \
	B_AXIS1B (UINavigate, Keyboard, keys::Arrow_Right, keys::Arrow_Left)      \
	B_STICK  (UINavigate, keys::PadAxis::LeftX, keys::PadAxis::LeftY, false, true) \
