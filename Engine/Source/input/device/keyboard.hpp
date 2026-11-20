/*************************************************************************************************************
\file       keyboard.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Keyboard: per-frame key state (held, pressed, released, repeated) and the modifier mask,
            fed from the raw event queue at one defined point in the frame.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Device state
#include "button_bank.hpp"

// Key codes
#include "../definitions/input_def.hpp"


namespace input::device
{
	/**
	 * Keyboard state for one frame.
	 *
	 * Fed from the GLFW key callback by way of the raw event queue, so it is
	 * updated at one defined point in the frame rather than at whatever depth
	 * glfwPollEvents happened to dispatch from.
	 *
	 */
	class Keyboard
	{
	public:

		/*!
			@brief
				Clears this frame's edges, keeping held state.
			*/
		void beginFrame()
		{
			bank_.beginFrame();
		}

		/*!
			@brief
				Applies one key event.
			@param key
				input::keys::KeyCode, which is GLFW's own key value
			@param action
				GLFW_PRESS, GLFW_RELEASE or GLFW_REPEAT
			@param mods
				input::keys::Mod bitmask reported alongside the event
			*/
		void onKey(int key, int action, int mods);

		/*!
			@brief
				Drops held keys without producing release edges.

				Called on focus loss so keys do not stay stuck down across an
				alt-tab, which would leave the camera drifting.
			*/
		void onFocusLost()
		{
			bank_.clearHeld();
			mods_ = keys::Mod::Default;
		}

		/*!
			@brief
				Whether a key is held.
			@param key
				input::keys::KeyCode
			@return
				true while held
			*/
		bool down(int key) const		{ return bank_.down(static_cast<std::size_t>(key)); }
		/*!
			@brief
				Whether a key went down this frame.
			@param key
				input::keys::KeyCode
			@return
				true on the press frame
			*/
		bool pressed(int key) const		{ return bank_.pressed(static_cast<std::size_t>(key)); }
		/*!
			@brief
				Whether a key came up this frame.
			@param key
				input::keys::KeyCode
			@return
				true on the release frame
			*/
		bool released(int key) const	{ return bank_.released(static_cast<std::size_t>(key)); }
		/*!
			@brief
				Whether a key auto-repeated this frame.
			@param key
				input::keys::KeyCode
			@return
				true when a repeat arrived this frame
			*/
		bool repeated(int key) const	{ return bank_.repeated(static_cast<std::size_t>(key)); }

		/*!
			@brief
				Modifier keys held as of the most recent event.
			@return
				input::keys::Mod bitmask
			*/
		int mods() const
		{
			return mods_;
		}

		/*!
			@brief
				Whether every modifier in a mask is held.
			@param mask
				input::keys::Mod bitmask. Mod::Default matches anything.
			@return
				true when the chord is satisfied
			*/
		bool modsHeld(int mask) const
		{
			return mask == keys::Mod::Default || (mods_ & mask) == mask;
		}

		/*!
			@brief
				Hides a key from every later reader this frame.
			@param key
				Key to swallow
			*/
		void consume(int key)
		{
			bank_.consume(static_cast<std::size_t>(key));
		}

		/*!
			@brief
				Refreshes the modifier mask from an event that carries one.
			@param mods
				input::keys::Mod bitmask
			*/
		void setMods(int mods)
		{
			mods_ = mods;
		}

	private:

		ButtonBank<keys::kMaxKeys>	bank_	{};

		int							mods_	{ keys::Mod::Default };
	};
}
