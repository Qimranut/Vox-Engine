/*************************************************************************************************************
\file       action_list.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      VOX_ACTION_LIST, the single list of every input action, and the Action id, value-type,
            default-context and name tables generated from it, plus name / fromName / componentCount.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// stds
#include <string>
#include <cstdint>
#include <string_view>
#include <unordered_map>


namespace input::action
{
	/**
	 * Shape an action resolves to.
	 *
	 * Decides which events the action publishes: a Button gets
	 * ActionPerformed on press, an Axis1/2/3 gets ActionValue every frame it
	 * is actuated. A Button never publishes ActionValue.
	 *
	 */
	enum class ValueType : std::uint8_t
	{
		Button,
		Axis1,
		Axis2,
		Axis3
	};

	/**
	 * Action map an action belongs to.
	 *
	 * Maps are stacked by priority at runtime, so a UI map pushed on top can
	 * swallow the sources the camera would otherwise read. Order here is
	 * declaration order only, not priority.
	 *
	 */
	enum class Context : std::uint8_t
	{
		Gameplay,
		Camera,
		Editor,
		UI,

		Count
	};
}


// ---------------------------------------------------------------------------
//  Every action the engine knows about.
//
//  Adding one is a single line here: the id, the value-type table, the default
//  context table, the name table and the Lua `Action` table are all generated
//  from this list, so they cannot fall out of step.
//
//  LookMouse and LookStick are two actions rather than one `Look` because
//  their UNITS differ irreconcilably - pixels-this-frame versus a rate per
//  second. See controller_input.hpp. Do not add a merged alias; it would be
//  the one place somebody adds the two together.
//
//     name                  value shape   default context
// ---------------------------------------------------------------------------

#define VOX_ACTION_LIST \
	X (None,                 Button, Gameplay)                                  \
	/* ----------------------------- Camera ----------------------------- */   \
	X (Move,                 Axis3,  Camera)                                    \
	X (LookMouse,            Axis2,  Camera)    /* PIXELS, frame-absolute */    \
	X (LookStick,            Axis2,  Camera)    /* RATE, per second       */    \
	X (LookModifier,         Button, Camera)                                    \
	X (Sprint,               Button, Camera)                                    \
	X (ZoomTicks,            Axis1,  Camera)    /* wheel, frame-absolute  */    \
	X (ZoomRate,             Axis1,  Camera)    /* trigger/dpad, per sec  */    \
	X (ToggleCameraMode,     Button, Camera)                                    \
	/* ---------------------------- Gameplay ---------------------------- */   \
	X (Fire,                 Button, Gameplay)                                  \
	X (Jump,                 Button, Gameplay)                                  \
	X (Interact,             Button, Gameplay)                                  \
	/* ------------------------------- UI ------------------------------- */   \
	X (UINavigate,           Axis2,  UI)                                        \
	X (UISubmit,             Button, UI)                                        \
	X (UICancel,             Button, UI)                                        \
	/* ----------------------------- Editor ----------------------------- */   \
	X (EditorFocusSelection, Button, Editor)                                    \
	X (EditorToggleCursor,   Button, Editor)                                    \
	X (DebugInput,           Button, Editor)                                    \


namespace input::action
{
	enum Action : std::uint16_t
	{
#define X(name, valueType, ctx) name,
		VOX_ACTION_LIST
#undef X

		Count
	};

	inline constexpr ValueType kValueType[Count] =
	{
#define X(name, valueType, ctx) ValueType::valueType,
		VOX_ACTION_LIST
#undef X
	};

	inline constexpr Context kDefaultContext[Count] =
	{
#define X(name, valueType, ctx) Context::ctx,
		VOX_ACTION_LIST
#undef X
	};

	inline constexpr const char* kName[Count] =
	{
#define X(name, valueType, ctx) #name,
		VOX_ACTION_LIST
#undef X
	};

	/*!
		@brief
			Human-readable name of an action.
		@param a
			Action to name
		@return
			The identifier spelled in VOX_ACTION_LIST, or "None" when out of range
		*/
	inline const char* name(Action a)
	{
		return (a < Count) ? kName[a] : kName[None];
	}

	/*!
		@brief
			Action id from its name, for Lua and for a future bindings file.
		@param n
			Identifier as spelled in VOX_ACTION_LIST, case sensitive
		@return
			The action, or Action::None when the name is unknown
		*/
	inline Action fromName(std::string_view n)
	{
		static const std::unordered_map<std::string, Action> map =
		{
#define X(name_, valueType, ctx) { #name_, Action::name_ },
			VOX_ACTION_LIST
#undef X
		};

		const auto it = map.find(std::string(n));

		return (it != map.end()) ? it->second : Action::None;
	}

	/*!
		@brief
			Number of value components an action carries.
		@param a
			Action to inspect
		@return
			1 for Button and Axis1, 2 for Axis2, 3 for Axis3
		*/
	inline constexpr int componentCount(Action a)
	{
		switch (kValueType[a])
		{
		case ValueType::Axis3:	return 3;
		case ValueType::Axis2:	return 2;
		default:				return 1;
		}
	}
}
