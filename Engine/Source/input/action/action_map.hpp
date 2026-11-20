/*************************************************************************************************************
\file       action_map.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      ActionMap (one prioritised set of bindings for a context), the device view and consumed-source
            set used by the resolver, and the free functions that evaluate, claim and merge bindings.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Bindings and resolved state
#include "binding.hpp"
#include "action_state.hpp"

// Devices
#include "../device/mouse.hpp"
#include "../device/gamepad.hpp"
#include "../device/keyboard.hpp"

// stds
#include <array>
#include <vector>
#include <bitset>


namespace input::action
{
	/**
	 * Read-only view of the devices, handed to the resolver.
	 *
	 * pad_ is the primary gamepad and may be null, which every evaluation
	 * path has to tolerate - a pad can vanish mid-frame.
	 *
	 */
	struct DeviceView
	{
		const device::Keyboard*	keyboard_	{ nullptr };
		const device::Mouse*	mouse_		{ nullptr };
		const device::Gamepad*	pad_		{ nullptr };
	};

	/**
	 * Sources already claimed by a higher-priority map this frame.
	 *
	 * Tracked separately from the devices so the devices stay const during
	 * resolution and a blocking context cannot corrupt the raw state that the
	 * debug overlay and the rebinding UI read.
	 *
	 */
	struct ConsumedSet
	{
		std::bitset<keys::kMaxKeys>			keys_	{};
		std::bitset<keys::kMaxMouseButtons>	mouse_	{};
		std::bitset<keys::kMaxPadButtons>	pad_	{};

		// Continuous channels, which have no button id of their own.
		bool	mouseDelta_	{ false };
		bool	scroll_		{ false };
		bool	leftStick_	{ false };
		bool	rightStick_	{ false };
		bool	triggers_	{ false };

		/*!
			@brief
				Releases every claimed source, button and continuous channel alike.
			*/
		void clear()
		{
			keys_.reset();
			mouse_.reset();
			pad_.reset();

			mouseDelta_ = false;
			scroll_ = false;
			leftStick_ = false;
			rightStick_ = false;
			triggers_ = false;
		}
	};

	/**
	 * One named set of bindings, stacked by priority at runtime.
	 *
	 */
	class ActionMap
	{
	public:

		/*!
			@brief
				Creates an empty, enabled, non-blocking map.
			@param c
				Context this map serves
			*/
		explicit ActionMap(Context c = Context::Gameplay)
			:
			context_{ c }
		{}

		/*!
			@brief
				Gets the context this map serves.
			@return
				The map's context
			*/
		Context context() const { return context_; }

		/*!
			@brief
				Assigns the context this map serves.

				Exists because the maps live in a fixed array indexed by Context,
				which has to be default constructed before each slot can be told
				which context it is.
			@param c
				Context to serve
			*/
		void setContext(Context c) { context_ = c; }

		/*!
			@brief
				Whether the map takes part in resolution.
			@return
				True if enabled
			*/
		bool enabled() const	{ return enabled_; }

		/*!
			@brief
				Whether the map swallows the sources it binds.
			@return
				True if blocking
			*/
		bool blocking() const	{ return blocking_; }

		/*!
			@brief
				Enables or disables the map.
			@param e
				True to enable
			*/
		void setEnabled(bool e)		{ enabled_ = e; }

		/*!
			@brief
				Whether this map swallows the sources it reads.

				A blocking map marks every source it BINDS as consumed, whether or
				not the binding actually fired. Consuming only on fire would let W
				fall through to the camera the moment an open menu had no use for
				it. Source-level rather than action-level, so a menu can swallow
				WASD without also swallowing a fullscreen toggle.
			@param b
				true to block lower-priority maps
			*/
		void setBlocking(bool b)	{ blocking_ = b; }

		/*!
			@brief
				Appends a binding to the map.
			@param b
				Binding to add
			*/
		void addBinding(const Binding& b)
		{
			bindings_.push_back(b);
		}

		/*!
			@brief
				Removes every binding driving one action.
			@param a
				Action to unbind
			*/
		void clearBindings(Action a);

		/*!
			@brief
				Gets the map's bindings in the order they were added.
			@return
				The binding list
			*/
		const std::vector<Binding>& bindings() const { return bindings_; }

		/*!
			@brief
				Removes every binding.
			*/
		void clear() { bindings_.clear(); }

	private:

		Context				context_	{ Context::Gameplay };
		bool				enabled_	{ true };
		bool				blocking_	{ false };

		std::vector<Binding>	bindings_;
	};

	/*!
		@brief
			Evaluates one binding against the devices.
		@param b
			Binding to evaluate
		@param view
			Current device state
		@return
			The binding's contribution, zeroed when its device is absent
		*/
	Candidate evaluate(const Binding& b, const DeviceView& view);

	/*!
		@brief
			Whether every source a binding reads is still unclaimed.
		@param b
			Binding to test
		@param consumed
			Sources already claimed this frame
		@return
			false when a higher-priority blocking map already took one of them
		*/
	bool isAvailable(const Binding& b, const ConsumedSet& consumed);

	/*!
		@brief
			Marks every source a binding reads as claimed.
		@param b
			Binding whose sources to claim
		@param consumed
			Set to mark
		*/
	void consumeSources(const Binding& b, ConsumedSet& consumed);

	/*!
		@brief
			Folds a binding's contribution into an action's state.

			Merges PER COMPONENT by larger magnitude, never by sum. Summing would
			mean WASD giving z = 1 plus a nudged stick giving z = 0.8 moving the
			camera at 1.8x speed merely because a pad is plugged in. Per component
			rather than per vector so that a binding covering only one axis - pad
			bumpers on world-up, say - does not wipe out a stick covering the other
			two.
		@param state
			Action state to fold into
		@param cand
			Contribution to merge
		*/
	void mergeCandidate(ActionState& state, const Candidate& cand);

	/*!
		@brief
			The compile-time default binding table.
		@return
			Every entry of VOX_DEFAULT_BINDING_LIST, in declaration order
		*/
	const std::vector<Binding>& defaultBindings();
}
