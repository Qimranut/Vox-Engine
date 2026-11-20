/*************************************************************************************************************
\file       gamepad.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Gamepad implementation: connect / disconnect / focus handling, polling GLFW's mapped gamepad
            state into button edges, hysteresis for the virtual trigger buttons, and activity detection.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "gamepad.hpp"

// GLFW
#include <GLFW/glfw3.h>

// GLM
#include <glm/common.hpp>

// stds
#include <cmath>


namespace input::device
{
	namespace
	{
		// Coarse, only for "is the player touching this pad at all". The real
		// per-binding deadzone lives in the binding layer.
		constexpr float kActivityThreshold = 0.35f;
	}

	/*!
		@brief
			Converts a GLFW trigger axis reading ([-1, 1], -1 at rest) to travel.
		@param raw
			Axis value straight from glfwGetGamepadState
		@return
			Travel in [0, 1], 0 at rest
		*/
	float triggerToUnit(float raw)
	{
		return glm::clamp((raw + 1.f) * 0.5f, 0.f, 1.f);
	}

	/*!
		@brief
			Marks the pad present and queries its name and mapping status.
		*/
	void Gamepad::onConnected()
	{
		if (jid_ < 0)
			return;

		connected_ = true;
		isGamepad_ = glfwJoystickIsGamepad(jid_) == GLFW_TRUE;

		// Prefer the mapped gamepad name: it is the human-facing product name
		// rather than the raw HID string.
		const char* n = isGamepad_ ? glfwGetGamepadName(jid_)
								   : glfwGetJoystickName(jid_);

		name_ = n ? n : "Unknown";
	}

	/*!
		@brief
			Marks the pad gone and zeroes everything it was reporting.
		*/
	void Gamepad::onDisconnected()
	{
		connected_ = false;
		isGamepad_ = false;
		name_.clear();

		clearState();
	}

	/*!
		@brief
			Releases everything the pad reports and resyncs on the next poll, leaving it connected.
		*/
	void Gamepad::onFocusLost()
	{
		clearState();
		resync_ = true;
	}

	/*!
		@brief
			Zeroes the axes and drops every held button without release edges.
		*/
	void Gamepad::clearState()
	{
		// Zero BEFORE anyone reads. A pad unplugged with the right stick
		// pushed otherwise leaves the last polled axes in place forever, and
		// the camera spins until the process exits.
		axes_.fill(0.f);
		prevButtons_.fill(false);

		// clearHeld rather than release edges: the buttons did not come up so
		// much as stop existing.
		bank_.clearHeld();
	}

	/*!
		@brief
			Polls the pad, stores its raw axes and appends button and trigger edges to [out].
		@param out
			Raw event queue to append press and release edges to
		*/
	void Gamepad::poll(std::vector<RawEvent>& out)
	{
		if (!connected_ || !isGamepad_)
			return;

		GLFWgamepadstate st{};

		if (!glfwGetGamepadState(jid_, &st))
		{
			// Present but unmapped, or removed between the connect callback
			// and this poll.
			onDisconnected();
			return;
		}

		if (resync_)
		{
			// First poll after a focus loss: take the live state as the
			// baseline. The bank never saw these buttons go down, so their
			// eventual release is ignored rather than surfacing as an edge.
			for (int b = 0; b <= GLFW_GAMEPAD_BUTTON_LAST; ++b)
				prevButtons_[static_cast<std::size_t>(b)] = (st.buttons[b] == GLFW_PRESS);

			for (int a = 0; a <= GLFW_GAMEPAD_AXIS_LAST; ++a)
				axes_[static_cast<std::size_t>(a)] = st.axes[a];

			prevButtons_[static_cast<std::size_t>(keys::PadButton::LeftTrigger)] =
				triggerToUnit(axes_[static_cast<std::size_t>(keys::PadAxis::LeftTriggerAxis)]) >= kTriggerPress;

			prevButtons_[static_cast<std::size_t>(keys::PadButton::RightTrigger)] =
				triggerToUnit(axes_[static_cast<std::size_t>(keys::PadAxis::RightTriggerAxis)]) >= kTriggerPress;

			resync_ = false;
			return;
		}

		// ------------------------- Buttons --------------------------

		for (int b = 0; b <= GLFW_GAMEPAD_BUTTON_LAST; ++b)
		{
			const bool isDown = (st.buttons[b] == GLFW_PRESS);
			const auto index = static_cast<std::size_t>(b);

			if (isDown != prevButtons_[index])
				out.push_back(RawEvent::padButton(jid_, b, isDown));

			prevButtons_[index] = isDown;
		}

		// -------------------------- Axes ----------------------------

		for (int a = 0; a <= GLFW_GAMEPAD_AXIS_LAST; ++a)
			axes_[static_cast<std::size_t>(a)] = st.axes[a];

		// Triggers are analog but must bind anywhere a button can, so they
		// also drive two virtual button ids.
		emitTriggerButton(out,
			static_cast<int>(keys::PadButton::LeftTrigger),
			triggerToUnit(axes_[static_cast<std::size_t>(keys::PadAxis::LeftTriggerAxis)]));

		emitTriggerButton(out,
			static_cast<int>(keys::PadButton::RightTrigger),
			triggerToUnit(axes_[static_cast<std::size_t>(keys::PadAxis::RightTriggerAxis)]));
	}

	/*!
		@brief
			Emits an edge for a virtual trigger button using the press / release hysteresis band.
		@param out
			Raw event queue to append to
		@param button
			The virtual PadButton to drive
		@param travel
			Trigger travel in [0, 1]
		*/
	void Gamepad::emitTriggerButton(std::vector<RawEvent>& out, int button, float travel)
	{
		const auto index = static_cast<std::size_t>(button);
		const bool wasDown = prevButtons_[index];

		// Two thresholds: cross the high one to press, fall below the low one
		// to release. Between them the state simply holds.
		const bool isDown = wasDown ? (travel > kTriggerRelease)
									: (travel >= kTriggerPress);

		if (isDown != wasDown)
			out.push_back(RawEvent::padButton(jid_, button, isDown));

		prevButtons_[index] = isDown;
	}

	/*!
		@brief
			Raw axis reading, 0 for an out-of-range axis.
		@param axis
			input::keys::PadAxis
		@return
			Sticks on [-1, 1] with +Y DOWN, triggers on [-1, 1] with -1 at rest
		*/
	float Gamepad::axisRaw(int axis) const
	{
		if (axis < 0 || static_cast<std::size_t>(axis) >= axes_.size())
			return 0.f;

		return axes_[static_cast<std::size_t>(axis)];
	}

	/*!
		@brief
			Whether anything on this pad is currently actuated.
		@return
			true when a button is held or a stick is outside a coarse deadzone
		*/
	bool Gamepad::anyActivity() const
	{
		if (!connected_ || !isGamepad_)
			return false;

		if (bank_.anyDown())
			return true;

		// Only the four stick axes: the triggers rest at -1 raw, so they would
		// read as permanently active without the conversion.
		const int sticks[] =
		{
			static_cast<int>(keys::PadAxis::LeftX),
			static_cast<int>(keys::PadAxis::LeftY),
			static_cast<int>(keys::PadAxis::RightX),
			static_cast<int>(keys::PadAxis::RightY)
		};

		for (int a : sticks)
		{
			if (std::fabs(axisRaw(a)) > kActivityThreshold)
				return true;
		}

		return false;
	}
}
