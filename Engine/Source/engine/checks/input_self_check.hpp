/*************************************************************************************************************
\file       input_self_check.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares the startup self-check for the input system's pure maths (deadzones, trigger
            remapping, mouse-versus-stick look units).

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once


namespace engine::checks
{
	/*!
		@brief
			Verifies the input system's pure maths at startup.

			Deadzone shape, trigger remapping and the mouse-versus-stick look
			units are all things that fail silently and feel wrong rather than
			crash, so they are checked here where neither a window nor a
			gamepad is needed. Reports to stderr; never throws.
		*/
	void runInputSelfCheck();
}
