/*************************************************************************************************************
\file       lua_api_node.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Lua Node table: Node.Create for an empty node or one instantiated from a named archetype.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "lua_api.hpp"

// Engine
#include "../interface/lua_interface.hpp"
#include "../../editor/sys/editor_sys.hpp"

// stds
#include <string>


namespace scripting::lua
{
	/*!
		@brief
			Registers the Node table (Node.Create, with or without an archetype name).
		@param lua
			Lua interface to register into
		@param editorSys
			Engine-side scene tree system that creates the nodes
		*/
	void registerNodeApi(Interface& lua, editor::System& editorSys)
	{
		// ------------------- ECS API -------------------

		lua.linkOverload(
			"Node",
			"Create",

			// Create Empty Node
			[&editorSys]()
			{
				return editorSys.createNode();
			},

			// Create Node with an archetype
			[&editorSys](const std::string& archetypeName)
			{
				return editorSys.createNode(archetypeName);
			});
	}
}
