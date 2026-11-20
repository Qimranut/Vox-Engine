/*************************************************************************************************************
\file       lua_api_util.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Small helpers shared by the Lua binding files: entity id conversion between EnTT and Lua, and
            the unknown-phase report for Input.On* calls.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// EnTT
#include <entt.hpp>

// stds
#include <cstdint>
#include <iostream>
#include <string>


namespace scripting::lua
{
	/*!
		@brief
			Entity id as Lua sees it.
		@param e
			EnTT entity to convert
		@return
			The integral id; entt::null maps to 0xFFFFFFFF
		*/
	inline std::uint32_t toLuaEntity(entt::entity e)
	{
		return static_cast<std::uint32_t>(entt::to_integral(e));
	}

	/*!
		@brief
			Entity handle from the integer id a script passed.
		@param id
			Integral entity id from Lua
		@return
			The EnTT entity
		*/
	inline entt::entity fromLuaEntity(std::uint32_t id)
	{
		return static_cast<entt::entity>(id);
	}

	/*!
		@brief
			Reports an unrecognised phase string passed to an Input.On* call.
		@param func
			Lua function name, for the message
		@param phase
			The phase the script passed
		@return
			0, the invalid handle
		*/
	inline int badPhase(const char* func, const std::string& phase)
	{
		std::cerr << "[lua] Input." << func << ": unknown phase \"" << phase << "\"\n";
		return 0;
	}
}
