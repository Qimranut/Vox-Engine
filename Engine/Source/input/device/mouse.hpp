/*************************************************************************************************************
\file       mouse.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Mouse: per-frame button state, cursor position, and the cursor delta and wheel scroll
            accumulated across the frame.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Device state
#include "button_bank.hpp"

// Mouse codes
#include "../definitions/input_def.hpp"

// GLM
#include <glm/glm.hpp>


namespace input::device
{
	/**
	 * Mouse state for one frame.
	 *
	 * Position is absolute window pixels with the origin at the top-left and Y
	 * growing downwards; delta and scroll are accumulated across the frame and
	 * cleared by beginFrame().
	 *
	 */
	class Mouse
	{
	public:

		/*!
			@brief
				Clears this frame's edges and accumulated deltas.
			*/
		void beginFrame()
		{
			bank_.beginFrame();

			delta_ = { 0.f, 0.f };
			scroll_ = { 0.f, 0.f };
		}

		/*!
			@brief
				Applies one mouse button event.
			@param button
				input::keys::MouseCode
			@param action
				GLFW_PRESS or GLFW_RELEASE
			@param mods
				input::keys::Mod bitmask
			*/
		void onButton(int button, int action, int mods);

		/*!
			@brief
				Applies one cursor sample, accumulating the delta (the first sample only sets the position).
			@param x
				Cursor X in window pixels
			@param y
				Cursor Y in window pixels
			*/
		void onCursorPos(double x, double y)
		{
			const glm::vec2 p{ static_cast<float>(x), static_cast<float>(y) };

			if (firstSample_)
			{
				pos_ = p;
				firstSample_ = false;
				return;
			}

			// Accumulated, not assigned: GLFW can fire this several times
			// within a single frame and every one of them counts.
			delta_ += p - pos_;
			pos_ = p;
		}

		/*!
			@brief
				Applies one scroll event, accumulating it into this frame's scroll.
			@param xOffset
				Horizontal wheel movement in ticks
			@param yOffset
				Vertical wheel movement in ticks
			*/
		void onScroll(double xOffset, double yOffset)
		{
			scroll_ += glm::vec2{ static_cast<float>(xOffset),
								  static_cast<float>(yOffset) };
		}

		/*!
			@brief
				Drops held buttons without producing release edges.
			*/
		void onFocusLost()
		{
			bank_.clearHeld();
		}

		/*!
			@brief
				Discards the next cursor sample's delta.

				Armed at startup, on every cursor-mode change and on regaining
				focus. In all three cases the pointer has moved without the user
				moving the mouse, and the resulting several-hundred-pixel delta
				whips the camera around.
			*/
		void armNextSample()
		{
			firstSample_ = true;
		}

		/*!
			@brief
				Whether a mouse button is held.
			@param button
				input::keys::MouseCode
			@return
				true while held
			*/
		bool down(int button) const		{ return bank_.down(static_cast<std::size_t>(button)); }
		/*!
			@brief
				Whether a mouse button went down this frame.
			@param button
				input::keys::MouseCode
			@return
				true on the press frame
			*/
		bool pressed(int button) const	{ return bank_.pressed(static_cast<std::size_t>(button)); }
		/*!
			@brief
				Whether a mouse button came up this frame.
			@param button
				input::keys::MouseCode
			@return
				true on the release frame
			*/
		bool released(int button) const	{ return bank_.released(static_cast<std::size_t>(button)); }

		/*!
			@brief
				Cursor position in window pixels, top-left origin.
			@return
				The latest cursor position
			*/
		glm::vec2 position() const	{ return pos_; }
		/*!
			@brief
				Cursor movement accumulated this frame.
			@return
				The frame's cursor delta in pixels
			*/
		glm::vec2 delta() const		{ return delta_; }
		/*!
			@brief
				Wheel movement accumulated this frame.
			@return
				The frame's scroll in ticks
			*/
		glm::vec2 scroll() const	{ return scroll_; }

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

		/*!
			@brief
				Hides this frame's cursor movement from every later reader.
			*/
		void consumeDelta()
		{
			delta_ = { 0.f, 0.f };
		}

		/*!
			@brief
				Hides this frame's wheel movement from every later reader.
			*/
		void consumeScroll()
		{
			scroll_ = { 0.f, 0.f };
		}

	private:

		ButtonBank<keys::kMaxMouseButtons>	bank_	{};

		glm::vec2	pos_		{ 0.f };
		glm::vec2	delta_		{ 0.f };
		glm::vec2	scroll_		{ 0.f };

		bool		firstSample_{ true };
	};
}
