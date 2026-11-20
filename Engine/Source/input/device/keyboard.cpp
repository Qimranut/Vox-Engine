/*************************************************************************************************************
\file       keyboard.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Keyboard implementation: applies GLFW key events (press, release, repeat) to the key bank.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "keyboard.hpp"

// GLFW
#include <GLFW/glfw3.h>


namespace input::device
{
	/*!
		@brief
			Applies one key event, keeping repeats apart from presses.
		@param key
			input::keys::KeyCode, which is GLFW's own key value
		@param action
			GLFW_PRESS, GLFW_RELEASE or GLFW_REPEAT
		@param mods
			input::keys::Mod bitmask reported alongside the event
		*/
	void Keyboard::onKey(int key, int action, int mods)
	{
		mods_ = mods;

		const auto index = static_cast<std::size_t>(key);

		if (action == GLFW_PRESS)
		{
			bank_.onDown(index);
		}
		else if (action == GLFW_RELEASE)
		{
			bank_.onUp(index);
		}
		else if (action == GLFW_REPEAT)
		{
			// Repeats are what a text field or a held arrow key in a list
			// wants, and are exactly what a movement key must NOT see - a
			// repeat is not a fresh press. ButtonBank keeps them apart.
			bank_.onRepeat(index);
		}
	}
}
