/*************************************************************************************************************
\file       binding.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Binding (one device source feeding one action), the radial and scalar deadzones, stick
            resolution, and the constexpr factories the default binding table is built from.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Action ids
#include "../definitions/action_list.hpp"

// Device codes
#include "../definitions/input_def.hpp"

// GLM
#include <glm/glm.hpp>

// stds
#include <array>
#include <cmath>
#include <cstdint>


namespace input::action
{
	/**
	 * Which physical device a binding reads.
	 *
	 */
	enum class Device : std::uint8_t
	{
		Keyboard,
		Mouse,
		Gamepad,

		None
	};

	/**
	 * How a binding turns device state into a value.
	 *
	 */
	enum class SourceKind : std::uint8_t
	{
		KeyButton,			// one key
		MouseButton,		// one mouse button
		PadButton,			// one pad button, including the virtual triggers

		Axis1FromButtons,	// code_[0] positive, code_[1] negative
		Axis2FromButtons,	// +x, -x, +y, -y
		Axis3FromButtons,	// +x, -x, +y, -y, +z, -z

		PadAxis1,			// one stick axis, scalar deadzone
		PadTrigger1,		// one trigger axis, remapped [-1,1] -> [0,1]
		PadStick2,			// a stick pair, RADIAL deadzone

		MouseDelta2,		// accumulated cursor pixels this frame
		MouseScroll1		// wheel ticks this frame
	};

	/**
	 * One source feeding one action.
	 *
	 * An action may carry many bindings - that is exactly how a control works
	 * on keyboard and pad at once. Bindings never sum; see ActionMap.
	 *
	 */
	struct Binding
	{
		Action		action_	{ Action::None };
		SourceKind	kind_	{ SourceKind::KeyButton };
		Device		device_	{ Device::Keyboard };

		// Up to six device codes or axis indices. Meaning depends on kind_.
		std::array<std::int16_t, 6> code_ { { -1, -1, -1, -1, -1, -1 } };

		// Modifier chord this binding requires. Mod::Default (0) means "do not
		// care", NOT "no modifiers held" - otherwise every binding would break
		// the moment the player rested a finger on shift.
		int		requiredMods_	{ keys::Mod::Default };

		// Per-axis sign flips, applied AFTER the deadzone so that the deadzone
		// stays symmetric.
		bool	invertX_		{ false };
		bool	invertY_		{ false };

		float	scale_			{ 1.f };

		// Stick deadzone band. Ignored by the button and mouse kinds.
		float	innerDeadzone_	{ 0.20f };
		float	outerDeadzone_	{ 0.95f };

		// Magnitude at which an analog source counts as "pressed".
		float	actuation_		{ 0.5f };
	};

	/*!
		@brief
			Radial deadzone with rescale.

			Radial rather than per-axis: a per-axis deadzone carves a SQUARE dead
			region out of a ROUND stick, so a gentle diagonal push registers on one
			axis only and the player walks along a cardinal instead of the diagonal
			they actually pushed.

			Rescaled rather than clamped: without remapping [inner, outer] onto
			[0, 1] the value jumps discontinuously from 0 to `inner` the instant the
			stick leaves the zone, and never reaches 1.0, because real sticks top
			out short of the corners.
		@param raw
			Stick vector straight from glfwGetGamepadState
		@param inner
			Magnitude below which the stick reads as centred
		@param outer
			Magnitude at and above which the stick reads as fully deflected
		@return
			Vector with magnitude in [0, 1], direction preserved exactly
		*/
	inline glm::vec2 applyRadialDeadzone(glm::vec2 raw, float inner, float outer)
	{
		const float mag = glm::length(raw);

		if (mag <= inner || mag <= 1e-6f)
			return { 0.f, 0.f };

		const float span = glm::max(outer - inner, 1e-6f);
		const float t = glm::min((mag - inner) / span, 1.f);

		// Divide by mag rather than normalize(): the direction survives
		// exactly and we reuse the length already paid for.
		return (raw / mag) * t;
	}

	/*!
		@brief
			Scalar equivalent of applyRadialDeadzone, for a lone axis.
		@param raw
			Axis value on [-1, 1]
		@param inner
			Lower edge of the deadzone band, applied to the magnitude
		@param outer
			Upper edge of the deadzone band, applied to the magnitude
		@return
			Value on [-1, 1], 0 inside the band
		*/
	inline float applyScalarDeadzone(float raw, float inner, float outer)
	{
		const float mag = std::fabs(raw);

		if (mag <= inner)
			return 0.f;

		const float span = glm::max(outer - inner, 1e-6f);
		const float t = glm::min((mag - inner) / span, 1.f);

		return (raw < 0.f) ? -t : t;
	}

	/*!
		@brief
			Resolves a PadStick2 binding's raw stick reading into an action value.

			Deadzone, then scale, then inverts, then placement. A stick has no
			up/down of its own, so bound to a three-component action (Move: x right,
			y world up, z forward) it drives the ground plane and its second axis
			lands in z, leaving y to other bindings. Placing it in y instead made
			the left stick fly the camera vertically and never walk the player
			forward. Bound to a two-component action it stays (x, y).
		@param b
			The stick binding
		@param raw
			Stick vector straight from glfwGetGamepadState, +Y down
		@return
			The value for b.action_
		*/
	inline glm::vec3 resolveStick(const Binding& b, glm::vec2 raw)
	{
		glm::vec2 v = applyRadialDeadzone(raw, b.innerDeadzone_, b.outerDeadzone_) * b.scale_;

		// Before placement, so invertY_ follows the second axis into z.
		if (b.invertX_)
			v.x = -v.x;

		if (b.invertY_)
			v.y = -v.y;

		return componentCount(b.action_) == 3
			? glm::vec3{ v.x, 0.f, v.y }
			: glm::vec3{ v.x, v.y, 0.f };
	}

	// ---------------------- Binding constructors ----------------------
	//
	// constexpr factories rather than aggregate initialisation, so the default
	// binding table can be built at compile time without every entry having to
	// spell out all eleven fields.

	/*!
		@brief
			A plain button binding; the source kind follows the device.
		@param a
			Action to drive
		@param d
			Device the code belongs to
		@param code
			Key, mouse button or pad button
		@param mods
			Required modifier chord, Mod::Default for "do not care"
		@return
			The binding
		*/
	constexpr Binding btn(Action a, Device d, int code, int mods = keys::Mod::Default)
	{
		Binding b;

		b.action_ = a;
		b.device_ = d;
		b.requiredMods_ = mods;

		b.kind_ = (d == Device::Keyboard) ? SourceKind::KeyButton
				: (d == Device::Mouse)	  ? SourceKind::MouseButton
										  : SourceKind::PadButton;

		b.code_[0] = static_cast<std::int16_t>(code);

		return b;
	}

	/*!
		@brief
			A one-dimensional axis built from a positive and a negative button.
		@param a
			Action to drive
		@param d
			Device both codes belong to
		@param pos
			Button for the positive direction
		@param neg
			Button for the negative direction
		@return
			The binding
		*/
	constexpr Binding axis1b(Action a, Device d, int pos, int neg)
	{
		Binding b;

		b.action_ = a;
		b.device_ = d;
		b.kind_ = SourceKind::Axis1FromButtons;
		b.code_[0] = static_cast<std::int16_t>(pos);
		b.code_[1] = static_cast<std::int16_t>(neg);

		return b;
	}

	/*!
		@brief
			A three-dimensional axis built from six buttons.

			The result is deliberately NOT normalized: two buttons held give a
			vector of length 1.41. Normalizing here would destroy a stick's genuine
			analog magnitude, and the camera controllers already normalize.
		@param a
			Action to drive
		@param d
			Device all codes belong to
		@param px
			Button for +x
		@param nx
			Button for -x
		@param py
			Button for +y
		@param ny
			Button for -y
		@param pz
			Button for +z
		@param nz
			Button for -z
		@return
			The binding
		*/
	constexpr Binding axis3b(Action a, Device d,
		int px, int nx, int py, int ny, int pz, int nz)
	{
		Binding b;

		b.action_ = a;
		b.device_ = d;
		b.kind_ = SourceKind::Axis3FromButtons;
		b.code_[0] = static_cast<std::int16_t>(px);
		b.code_[1] = static_cast<std::int16_t>(nx);
		b.code_[2] = static_cast<std::int16_t>(py);
		b.code_[3] = static_cast<std::int16_t>(ny);
		b.code_[4] = static_cast<std::int16_t>(pz);
		b.code_[5] = static_cast<std::int16_t>(nz);

		return b;
	}

	/*!
		@brief
			A thumbstick pair, radially deadzoned.
		@param a
			Action to drive
		@param axisX
			Pad axis for the stick's x
		@param axisY
			Pad axis for the stick's y
		@param invertX
			Flip the x component
		@param invertY
			Flip the y component
		@return
			The binding
		*/
	constexpr Binding stick(Action a, int axisX, int axisY,
		bool invertX = false, bool invertY = false)
	{
		Binding b;

		b.action_ = a;
		b.device_ = Device::Gamepad;
		b.kind_ = SourceKind::PadStick2;
		b.code_[0] = static_cast<std::int16_t>(axisX);
		b.code_[1] = static_cast<std::int16_t>(axisY);
		b.invertX_ = invertX;
		b.invertY_ = invertY;

		return b;
	}

	/*!
		@brief
			A single stick axis with a scalar deadzone.
		@param a
			Action to drive
		@param axis
			Pad axis to read
		@param invert
			Flip the value
		@return
			The binding
		*/
	constexpr Binding padAxis(Action a, int axis, bool invert = false)
	{
		Binding b;

		b.action_ = a;
		b.device_ = Device::Gamepad;
		b.kind_ = SourceKind::PadAxis1;
		b.code_[0] = static_cast<std::int16_t>(axis);
		b.invertX_ = invert;

		return b;
	}

	/*!
		@brief
			A trigger read as an analog value on [0, 1].
		@param a
			Action to drive
		@param axis
			Trigger axis to read
		@return
			The binding, with a small jitter-only deadzone
		*/
	constexpr Binding padTrigger(Action a, int axis)
	{
		Binding b;

		b.action_ = a;
		b.device_ = Device::Gamepad;
		b.kind_ = SourceKind::PadTrigger1;
		b.code_[0] = static_cast<std::int16_t>(axis);

		// Triggers rest clean after the conversion; only jitter needs killing.
		b.innerDeadzone_ = 0.05f;
		b.outerDeadzone_ = 1.f;

		return b;
	}

	/*!
		@brief
			This frame's accumulated cursor movement, in pixels.
		@param a
			Action to drive
		@return
			The binding; any movement at all actuates it
		*/
	constexpr Binding mouseDelta(Action a)
	{
		Binding b;

		b.action_ = a;
		b.device_ = Device::Mouse;
		b.kind_ = SourceKind::MouseDelta2;

		// Pixels, not a normalized axis: a one-pixel nudge is real input.
		b.actuation_ = 0.f;

		return b;
	}

	/*!
		@brief
			This frame's wheel movement, in ticks.
		@param a
			Action to drive
		@return
			The binding; any movement at all actuates it
		*/
	constexpr Binding mouseScroll(Action a)
	{
		Binding b;

		b.action_ = a;
		b.device_ = Device::Mouse;
		b.kind_ = SourceKind::MouseScroll1;
		b.actuation_ = 0.f;

		return b;
	}
}
