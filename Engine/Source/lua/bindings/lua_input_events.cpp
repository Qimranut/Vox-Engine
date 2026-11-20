/*************************************************************************************************************
\file       lua_input_events.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      InputEventBridge implementation: construction and removal of the bus subscriptions scripts own.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "lua_input_events.hpp"

namespace scripting::lua
{
	/*!
		@brief
			Creates an empty bridge over the bus.
		@param bus
			Event bus the script subscriptions are made on
		*/
	InputEventBridge::InputEventBridge(::events::Bus& bus)
		:
		bus_{ bus }
	{
	}

	/*!
		@brief
			Cancels one script subscription; unknown handles are ignored.
		@param handle
			Handle from subscribe()
		*/
	void InputEventBridge::remove(int handle)
	{
		subs_.erase(handle);
	}

	/*!
		@brief
			Cancels every script subscription.
		*/
	void InputEventBridge::clear()
	{
		subs_.clear();
	}
}
