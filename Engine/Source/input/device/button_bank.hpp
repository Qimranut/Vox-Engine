/*************************************************************************************************************
\file       button_bank.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      ButtonBank: held / pressed / released / repeated state for one device's buttons, with edges
            latched from the event stream so a press and release inside one frame both register.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// stds
#include <bitset>
#include <cstddef>


namespace input::device
{
	/**
	 * Held / pressed / released state for one device's buttons.
	 *
	 * Edges are LATCHED from the event stream rather than diffed against the
	 * previous frame. A button that goes down and back up inside a single
	 * frame is indistinguishable from an untouched one under a
	 * previous-versus-current diff - both edges are lost. Latching reports
	 * pressed() AND released() for that frame, which is what a menu click at
	 * 20 fps actually needs.
	 *
	 * Polled devices get the same guarantee by diffing their own poll against
	 * their own previous poll and pushing the resulting edges through
	 * onDown() / onUp(), so there is exactly one edge rule in the engine.
	 *
	 */
	template <std::size_t N>
	class ButtonBank
	{
	public:

		/*!
			@brief
				Clears this frame's edges, keeping held state.

				Runs before the frame's events are drained, so every reader in the
				frame sees the same view.
			*/
		void beginFrame()
		{
			pressed_.reset();
			released_.reset();
			repeated_.reset();
		}

		/*!
			@brief
				Records a button going down; only a transition counts as a press.
			@param i
				Button index. Out of range is ignored rather than asserted - device
				codes come from the OS and a driver may report anything.
			*/
		void onDown(std::size_t i)
		{
			if (i >= N)
				return;

			// An auto-repeat arrives as another down. Only the transition is
			// a press, so a held key does not read as pressed() every frame.
			if (!down_.test(i))
				pressed_.set(i);

			down_.set(i);
		}

		/*!
			@brief
				Records a button coming up, latching a release if it was held.
			@param i
				Button index
			*/
		void onUp(std::size_t i)
		{
			if (i >= N)
				return;

			if (down_.test(i))
				released_.set(i);

			down_.reset(i);
		}

		/*!
			@brief
				Records an auto-repeat, which is deliberately not a press.
			@param i
				Button index
			*/
		void onRepeat(std::size_t i)
		{
			if (i < N)
				repeated_.set(i);
		}

		/*!
			@brief
				Whether a button is currently held.
			@param i
				Button index
			@return
				true when the button is down and in range
			*/
		bool down(std::size_t i) const		{ return i < N && down_.test(i); }
		/*!
			@brief
				Whether a button went down this frame.
			@param i
				Button index
			@return
				true when a press edge was latched this frame
			*/
		bool pressed(std::size_t i) const	{ return i < N && pressed_.test(i); }
		/*!
			@brief
				Whether a button came up this frame.
			@param i
				Button index
			@return
				true when a release edge was latched this frame
			*/
		bool released(std::size_t i) const	{ return i < N && released_.test(i); }
		/*!
			@brief
				Whether a button auto-repeated this frame.
			@param i
				Button index
			@return
				true when a repeat was recorded this frame
			*/
		bool repeated(std::size_t i) const	{ return i < N && repeated_.test(i); }

		/*!
			@brief
				Whether anything at all is held.
			@return
				true when at least one button is down
			*/
		bool anyDown() const
		{
			return down_.any();
		}

		/*!
			@brief
				Drops every held button WITHOUT producing release edges.

				For focus loss and device removal, where the buttons did not come up
				so much as stop existing. Producing releases there would fire
				"player let go of fire" for a pad that was yanked out.
			*/
		void clearHeld()
		{
			down_.reset();
		}

		/*!
			@brief
				Hides a button from every later reader this frame.

				Used by a blocking action context to swallow a source so lower
				priority maps never see it.
			@param i
				Button index
			*/
		void consume(std::size_t i)
		{
			if (i >= N)
				return;

			pressed_.reset(i);
			released_.reset(i);
			down_.reset(i);
		}

	private:

		std::bitset<N> down_		{};
		std::bitset<N> pressed_		{};
		std::bitset<N> released_	{};
		std::bitset<N> repeated_	{};
	};
}
