/*************************************************************************************************************
\file       input_def.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      GLFW-free device code lists (keys, mouse buttons, pad buttons, pad axes) as X-macros, the
            enums and array sizes generated from them, modifier bits, and name-to-code lookup maps.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// stds
#include <string>
#include <cstddef>
#include <unordered_map>


// ---------------------------------------------------------------------------
//  Device code lists.
//
//  Every value here is GLFW's own, so a code can be used to index the arrays
//  the GLFW callbacks write into without a translation table. The lists are
//  prefixed VOX_ because they are object-like macros that survive every
//  include - an unprefixed KEYS collides with vendored and platform headers.
//
//  A matching static_assert block lives in input_def.cpp, so the tables below
//  cannot drift from the GLFW headers without a compile error.
// ---------------------------------------------------------------------------

#define VOX_KEY_LIST \
    X (_NULL, 0) \
    X (Num0, 48)                /* Number keys */ \
    X (Num1, 49) \
    X (Num2, 50) \
    X (Num3, 51) \
    X (Num4, 52) \
    X (Num5, 53) \
    X (Num6, 54) \
    X (Num7, 55) \
    X (Num8, 56) \
    X (Num9, 57) \
    X (Minus, 45)               /* Special keys */ \
    X (Equal, 61) \
    X (Left_Bracket, 91) \
    X (Right_Bracket, 93) \
    X (Backslash, 92) \
    X (Semicolon, 59) \
    X (Apostrophe, 39) \
    X (Comma, 44) \
    X (Period, 46) \
    X (Slash, 47) \
    X (Grave_Accent, 96) \
    X (A, 65)                   /* Alpha keys */ \
    X (B, 66) \
    X (C, 67) \
    X (D, 68) \
    X (E, 69) \
    X (F, 70) \
    X (G, 71) \
    X (H, 72) \
    X (I, 73) \
    X (J, 74) \
    X (K, 75) \
    X (L, 76) \
    X (M, 77) \
    X (N, 78) \
    X (O, 79) \
    X (P, 80) \
    X (Q, 81) \
    X (R, 82) \
    X (S, 83) \
    X (T, 84) \
    X (U, 85) \
    X (V, 86) \
    X (W, 87) \
    X (X, 88) \
    X (Y, 89) \
    X (Z, 90) \
    X (F1, 290)                 /* Function keys */ \
    X (F2, 291) \
    X (F3, 292) \
    X (F4, 293) \
    X (F5, 294) \
    X (F6, 295) \
    X (F7, 296) \
    X (F8, 297) \
    X (F9, 298) \
    X (F10, 299) \
    X (F11, 300) \
    X (F12, 301) \
    X (L_Shift, 340)            /* Modifier Keys */ \
    X (L_Control, 341) \
    X (L_Alt, 342) \
    X (L_Super, 343) \
    X (R_Shift, 344) \
    X (R_Control, 345) \
    X (R_Alt, 346) \
    X (R_Super, 347) \
    X (Space, 32)              /* Standard Keys */ \
    X (Escape, 256) \
    X (Enter, 257) \
    X (Tab, 258) \
    X (Backspace, 259) \
    X (Delete, 261) \
    X (Arrow_Right, 262)       /* Arrow keys - prefixed because an unscoped */ \
    X (Arrow_Left, 263)        /* Action::Down already lives in this namespace */ \
    X (Arrow_Down, 264) \
    X (Arrow_Up, 265) \

// Mouse buttons are a SEPARATE value space from VOX_KEY_LIST on purpose. GLFW
// numbers them 0..7, which collides with the key list's X(_NULL, 0) and, worse,
// would let Keyboard::down() happily index the key array with a mouse button.
#define VOX_MOUSE_LIST \
    X (Left_Mouse, 0) \
    X (Right_Mouse, 1) \
    X (Middle_Mouse, 2) \
    X (Mouse_4, 3) \
    X (Mouse_5, 4) \

// Face buttons are named by POSITION, not by label. A/B/X/Y is a Microsoft
// layout name: the same physical positions are Cross/Circle/Square/Triangle on
// a DualSense, and on a Switch pad A and B are physically swapped relative to
// Xbox while GLFW still reports index 0 for the bottom button. Naming the enum
// `A` guarantees somebody writes `down(A)` meaning "confirm" and ships a pad
// where that is wrong.
#define VOX_GAMEPAD_BUTTON_LIST \
    X (South, 0)                /* A / Cross      - confirm */ \
    X (East, 1)                 /* B / Circle     - cancel  */ \
    X (West, 2)                 /* X / Square               */ \
    X (North, 3)                /* Y / Triangle             */ \
    X (LeftBumper, 4) \
    X (RightBumper, 5) \
    X (Back, 6) \
    X (Start, 7) \
    X (Guide, 8) \
    X (LeftStickPress, 9) \
    X (RightStickPress, 10) \
    X (DPadUp, 11) \
    X (DPadRight, 12) \
    X (DPadDown, 13) \
    X (DPadLeft, 14) \
    X (LeftTrigger, 15)         /* Virtual: synthesized from the trigger AXES */ \
    X (RightTrigger, 16)        /* with hysteresis, so a trigger can bind     */ \
                                /* anywhere a button can.                     */

#define VOX_GAMEPAD_AXIS_LIST \
    X (LeftX, 0) \
    X (LeftY, 1)                /* +DOWN, see the sign table in default_bindings.hpp */ \
    X (RightX, 2) \
    X (RightY, 3)               /* +DOWN */ \
    X (LeftTriggerAxis, 4)      /* [-1, 1] with -1 at REST, not 0 */ \
    X (RightTriggerAxis, 5) \


namespace input::keys
{
    // ========================= Based on GLFW =========================

    // Array sizes. Spelled as constants rather than GLFW_*_LAST + 1 so that
    // headers declaring the device banks do not have to include GLFW.
    inline constexpr std::size_t kMaxKeys            = 349;   // GLFW_KEY_LAST + 1
    inline constexpr std::size_t kMaxMouseButtons    = 8;     // GLFW_MOUSE_BUTTON_LAST + 1
    inline constexpr std::size_t kMaxPadButtons      = 17;    // 15 real + 2 virtual triggers
    inline constexpr std::size_t kMaxPadAxes         = 6;     // GLFW_GAMEPAD_AXIS_LAST + 1
    inline constexpr std::size_t kMaxGamepads        = 16;    // GLFW_JOYSTICK_LAST + 1

    // Bit-for-bit GLFW's GLFW_MOD_*. Naming matched with C# end naming.
    enum Mod
    {
        Default = 0,
        Shift = 1 << 0,
        Control = 1 << 1,
        Alt = 1 << 2,
        Super = 1 << 3,
        CapsLock = 1 << 4,
        NumLock = 1 << 5
    };

    enum KeyCode : int
    {
#define X(name, value) name = value,
        VOX_KEY_LIST
#undef X
    };

    /*!
        @brief
            Name to key code lookup, for rebinding from Lua and from data files.
        @return
            Map keyed by the identifier spelled in VOX_KEY_LIST
        */
    inline const std::unordered_map<std::string, input::keys::KeyCode>& KeyMap()
    {
        static const std::unordered_map<std::string, input::keys::KeyCode> km =
        {
    #define X(name, value) { #name, input::keys::KeyCode::name },
                VOX_KEY_LIST
    #undef X
        };

        return km;
    }

    enum MouseCode : int
    {
#define X(name, value) name = value,
        VOX_MOUSE_LIST
#undef X
    };

    /*!
        @brief
            Name to mouse button lookup.
        @return
            Map keyed by the identifier spelled in VOX_MOUSE_LIST
        */
    inline const std::unordered_map<std::string, input::keys::MouseCode>& MouseMap()
    {
        static const std::unordered_map<std::string, input::keys::MouseCode> mm =
        {
    #define X(name, value) { #name, input::keys::MouseCode::name },
                VOX_MOUSE_LIST
    #undef X
        };

        return mm;
    }

    // Scoped, unlike KeyCode/MouseCode: these are new, so there is no legacy
    // call site relying on the unscoped spelling.
    enum class PadButton : int
    {
#define X(name, value) name = value,
        VOX_GAMEPAD_BUTTON_LIST
#undef X
    };

    enum class PadAxis : int
    {
#define X(name, value) name = value,
        VOX_GAMEPAD_AXIS_LIST
#undef X
    };

    /*!
        @brief
            Name to gamepad button lookup.
        @return
            Map keyed by the identifier spelled in VOX_GAMEPAD_BUTTON_LIST
        */
    inline const std::unordered_map<std::string, input::keys::PadButton>& PadButtonMap()
    {
        static const std::unordered_map<std::string, input::keys::PadButton> pm =
        {
#define X(name, value) { #name, input::keys::PadButton::name },
                VOX_GAMEPAD_BUTTON_LIST
#undef X
        };

        return pm;
    }

    /*!
        @brief
            Name to gamepad axis lookup.
        @return
            Map keyed by the identifier spelled in VOX_GAMEPAD_AXIS_LIST
        */
    inline const std::unordered_map<std::string, input::keys::PadAxis>& PadAxisMap()
    {
        static const std::unordered_map<std::string, input::keys::PadAxis> pm =
        {
#define X(name, value) { #name, input::keys::PadAxis::name },
                VOX_GAMEPAD_AXIS_LIST
#undef X
        };

        return pm;
    }
}
