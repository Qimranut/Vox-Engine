/*************************************************************************************************************
\file       gl_debug.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Enables and disables routing of the driver's KHR_debug messages to stderr (Debug builds only).

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once


namespace graphics::debug
{
	/*!
		@brief
			Routes the driver's KHR_debug messages to stderr as "[gl] ...".

			Debug builds only, and only on a context created with the debug flag
			(see window::System::createWindow). Messages are synchronous, so a
			breakpoint in the callback stops on the GL call that caused it.
			Notifications and known driver chatter are dropped. Logs, never asserts.
			Needs a current context.
		*/
	void enableGlDebugOutput();

	/*!
		@brief
			Detaches the callback. Call before the context goes away.
		*/
	void disableGlDebugOutput();
}
