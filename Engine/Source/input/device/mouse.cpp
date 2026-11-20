/*************************************************************************************************************
\file       mouse.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Mouse implementation: applies GLFW mouse button events to the button bank.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "mouse.hpp"

// GLFW
#include <GLFW/glfw3.h>


namespace input::device
{
	/*!
		@brief
			Applies one mouse button press or release; modifiers are ignored.
		@param button
			input::keys::MouseCode
		@param action
			GLFW_PRESS or GLFW_RELEASE
		@param mods
			input::keys::Mod bitmask (unused)
		*/
	void Mouse::onButton(int button, int action, int mods)
	{
		(void)mods;

		const auto index = static_cast<std::size_t>(button);

		if (action == GLFW_PRESS)
			bank_.onDown(index);
		else if (action == GLFW_RELEASE)
			bank_.onUp(index);
	}
}
