/*************************************************************************************************************
\file       lua_input_events.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      InputEventBridge: owns every bus subscription made from Lua, hands out handles for Input.Off,
            and calls Lua callbacks with script errors reported instead of thrown.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Event bus
#include "../../events/bus.hpp"

// Profiling
#include "../../profile/profiler.hpp"

// Sol
#include <sol/sol.hpp>

// stds
#include <functional>
#include <iostream>
#include <unordered_map>
#include <utility>


namespace scripting::lua
{
	/**
	 * Owns every bus subscription made from Lua.
	 *
	 * Scripts get input by registering callbacks (Input.OnAction and
	 * friends), and each of those becomes a bus subscription whose handler
	 * holds a Lua function. The ownership has to sit in C++:
	 *
	 *  - A sol function references the Lua state, so every subscription has
	 *    to be released while that state is still open. SystemManager
	 *    declares this after the Lua interface, so it is destroyed first.
	 *  - Reloading scripts replaces their environments, and callbacks into
	 *    the old ones must not keep firing. clear() before a reload.
	 *
	 * Main thread only, like the bus.
	 *
	 */
	class InputEventBridge
	{
	public:

		/*!
			@brief
				Creates an empty bridge over the bus.

				Fully qualified: scripting::lua has no events namespace today, but
				input does, and the same trap is worth avoiding here.
			@param bus
				Event bus the script subscriptions are made on
			*/
		explicit InputEventBridge(::events::Bus& bus);

		InputEventBridge(const InputEventBridge&) = delete;
		InputEventBridge& operator=(const InputEventBridge&) = delete;

		/*!
			@brief
				Subscribes a handler on behalf of a script.
			@param handler
				Called with every subsequently published Event
			@return
				Handle for Input.Off, never 0
			*/
		template <typename Event>
		int subscribe(std::function<void(const Event&)> handler)
		{
			const int handle = ++nextHandle_;

			subs_.emplace(handle,
				::events::ScopedSubscription{ bus_, bus_.subscribe<Event>(std::move(handler)) });

			return handle;
		}

		/*!
			@brief
				Cancels one script subscription.

				Safe from inside the callback being removed: the bus defers the
				physical removal until its dispatch returns.
			@param handle
				Handle from subscribe(). Unknown handles are ignored.
			*/
		void remove(int handle);

		/*!
			@brief
				Cancels every script subscription.
			*/
		void clear();

		/*!
			@brief
				Calls a Lua callback, reporting a script error instead of throwing
				it through the bus dispatch.
			@param fn
				Callback to invoke
			@param args
				Arguments forwarded to Lua
			*/
		template <typename... Args>
		static void call(const sol::protected_function& fn, Args&&... args)
		{
			// Every Lua callback the engine makes after startup comes through
			// here, so this one zone is "time spent in scripts" per frame.
			VOX_PROFILE_SCOPE_C("Lua callback", profile::colour::kScript);

			sol::protected_function_result result = fn(std::forward<Args>(args)...);

			if (!result.valid())
			{
				sol::error err = result;
				std::cerr << "[lua] input callback failed: " << err.what() << "\n";
			}
		}

	private:

		::events::Bus&	bus_;

		std::unordered_map<int, ::events::ScopedSubscription>	subs_;

		int	nextHandle_	{ 0 };
	};
}
