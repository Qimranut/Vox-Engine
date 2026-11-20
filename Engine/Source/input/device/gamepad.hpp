/*************************************************************************************************************
\file       gamepad.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Gamepad: one GLFW joystick slot seen through GLFW's SDL gamepad mapping, polled into button
            edges, plus triggerToUnit and the trigger hysteresis thresholds.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Device state
#include "button_bank.hpp"

// Raw event queue
#include "../events/raw_event.hpp"

// Pad codes
#include "../definitions/input_def.hpp"

// stds
#include <array>
#include <string>
#include <vector>


namespace input::device
{
	/*!
		@brief
			Converts a GLFW trigger axis reading to trigger travel.

			GLFW's gamepad mapping reports the triggers on [-1, 1] with -1 at REST,
			not 0. Binding that axis straight to an action leaves a permanent -1
			reading that looks like the trigger is held backwards, and any action
			bound to it fires forever. This is the single most common GLFW gamepad
			bug and the reason raw axes never leave this file unconverted.
		@param raw
			Axis value straight from glfwGetGamepadState
		@return
			Travel in [0, 1], 0 at rest
		*/
	float triggerToUnit(float raw);

	// Hysteresis band for the virtual trigger buttons. Two thresholds rather
	// than one: a trigger resting exactly on a single threshold machine-guns
	// press/release edges every frame.
	inline constexpr float kTriggerPress	= 0.60f;
	inline constexpr float kTriggerRelease	= 0.40f;

	/**
	 * One GLFW joystick slot, seen through GLFW 3.4's SDL gamepad mapping.
	 *
	 * A joystick that is present but has no mapping - a flight stick, a dance
	 * mat - is tracked as connected but never produces state, because GLFW
	 * cannot tell us which of its axes is a thumbstick.
	 *
	 */
	class Gamepad
	{
	public:

		/*!
			@brief
				Binds this object to a joystick slot. Called once at startup.
			@param jid
				GLFW joystick id
			*/
		void setSlot(int jid)
		{
			jid_ = jid;
		}

		/*!
			@brief
				Clears this frame's button edges, keeping held state.
			*/
		void beginFrame()
		{
			bank_.beginFrame();
		}

		/*!
			@brief
				Marks the pad present and queries its name and mapping status.

				Idempotent, so enumerating at startup and then receiving a connect
				callback for the same pad is harmless.
			*/
		void onConnected();

		/*!
			@brief
				Marks the pad gone and zeroes everything it was reporting.
			*/
		void onDisconnected();

		/*!
			@brief
				Releases everything the pad reports while leaving it connected.

				The window losing focus takes the player's input away, not the pad,
				so the pad must not be marked disconnected: GLFW only reports a
				joystick again when it is physically replugged. The next poll adopts
				the pad's live state as its baseline without emitting edges, so a
				button held through an alt-tab neither presses on return nor
				releases later.
			*/
		void onFocusLost();

		/*!
			@brief
				Polls the pad and appends any button edges to [out].

				Axes are stored raw. Deadzone and trigger conversion happen in the
				binding layer so that a rebind can carry its own deadzone.
			@param out
				Raw event queue to append press and release edges to
			*/
		void poll(std::vector<RawEvent>& out);

		/*!
			@brief
				Whether a joystick is present in this slot.
			@return
				true while connected
			*/
		bool connected() const	{ return connected_; }
		/*!
			@brief
				Whether the joystick has a gamepad mapping.
			@return
				true when GLFW maps it as a gamepad
			*/
		bool isGamepad() const	{ return isGamepad_; }
		/*!
			@brief
				GLFW joystick id this object is bound to.
			@return
				The joystick id, -1 if unbound
			*/
		int  slot() const		{ return jid_; }

		/*!
			@brief
				Human-facing name of the pad.
			@return
				The pad's name, empty when disconnected
			*/
		const std::string& name() const { return name_; }

		/*!
			@brief
				Whether a pad button is held.
			@param button
				input::keys::PadButton
			@return
				true while held
			*/
		bool down(int button) const		{ return bank_.down(static_cast<std::size_t>(button)); }
		/*!
			@brief
				Whether a pad button went down this frame.
			@param button
				input::keys::PadButton
			@return
				true on the press frame
			*/
		bool pressed(int button) const	{ return bank_.pressed(static_cast<std::size_t>(button)); }
		/*!
			@brief
				Whether a pad button came up this frame.
			@param button
				input::keys::PadButton
			@return
				true on the release frame
			*/
		bool released(int button) const	{ return bank_.released(static_cast<std::size_t>(button)); }

		/*!
			@brief
				Raw axis reading.
			@param axis
				input::keys::PadAxis
			@return
				Sticks on [-1, 1] with +Y DOWN, triggers on [-1, 1] with -1 at rest
			*/
		float axisRaw(int axis) const;

		/*!
			@brief
				Applies a button edge produced by poll().

				Separate from poll() because the edges travel through the raw queue
				first, so that a pad button and a key pressed in the same frame land
				in the devices in a single, ordered pass.
			@param button
				input::keys::PadButton
			@param isDown
				true for a press
			*/
		void applyButton(int button, bool isDown)
		{
			const auto index = static_cast<std::size_t>(button);

			if (isDown)
				bank_.onDown(index);
			else
				bank_.onUp(index);
		}

		/*!
			@brief
				Whether anything on this pad is currently actuated.
			@return
				true when a button is held or a stick is outside a coarse deadzone
			*/
		bool anyActivity() const;

		/*!
			@brief
				Hides a button from every later reader this frame.
			@param button
				Button to swallow
			*/
		void consume(int button)
		{
			bank_.consume(static_cast<std::size_t>(button));
		}

	private:

		int			jid_		{ -1 };
		bool		connected_	{ false };
		bool		isGamepad_	{ false };
		bool		resync_		{ false };
		std::string	name_		{};

		ButtonBank<keys::kMaxPadButtons>			bank_			{};
		std::array<float, keys::kMaxPadAxes>		axes_			{};

		// Previous poll, for turning polled state into edges. Only covers the
		// real GLFW buttons; the virtual triggers latch on their own band.
		std::array<bool, keys::kMaxPadButtons>		prevButtons_	{};

		/*!
			@brief
				Zeroes the axes and drops every held button without release edges.
			*/
		void clearState();

		/*!
			@brief
				Emits an edge for a virtual trigger button using the hysteresis
				band, so it behaves like a button without chattering.
			@param out
				Raw event queue to append to
			@param button
				The virtual PadButton to drive
			@param travel
				Trigger travel in [0, 1]
			*/
		void emitTriggerButton(std::vector<RawEvent>& out, int button, float travel);
	};
}
