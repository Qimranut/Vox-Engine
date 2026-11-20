/*************************************************************************************************************
\file       lua_api.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      registerEngineApi: registers every Lua table, handing each register function only the
            subsystems it needs from SystemManager.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "lua_api.hpp"

// Engine
#include "../../engine/systems/system_manager.hpp"


namespace scripting::lua
{
	/*!
		@brief
			Registers the Audio, Input, Camera, Physics, Node and Voxel tables.
		@param systems
			Constructed subsystems with the Lua interface initialised
		*/
	void registerEngineApi(engine::SystemManager& systems)
	{
		Interface& lua = systems.lua();
		InputEventBridge& bridge = systems.luaEvents();

		registerAudioApi(lua, systems.bus(), systems.scenes());
		registerInputApi(lua, bridge, systems.inputSys());
		registerCameraApi(lua, systems.graphicsSys());
		registerPhysicsApi(lua, bridge, systems.physicsSys());
		registerNodeApi(lua, systems.editorSys());
		registerVoxelApi(lua, systems.voxelSys());
	}
}
