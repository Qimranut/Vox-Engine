#pragma once

// Scene Tree
#include "../scene_tree/scene_tree.hpp"

// ECS
#include "../../ecs/ecs_manager/ecs_mng.hpp"

// Lua
#include "../../lua/interface/lua_interface.hpp"

// stds
#include <iostream>


namespace editor
{
	class System
	{
	public:

		System(ecs::ECSManager&);

		/**
		 * Create an empty node.
		 * 
		 */
		void createNode();

		/**
		 * Create a node out of an archetype
		 * 
		 * [param] 
		 * Archetype to instantiate an entity with
		 * 
		 */
		void createNode(ecs::Archetype&);

		/**
		 * Create a node out of an archetype, by name
		 *
		 * [param]
		 * Archetype name
		 *
		 */
		void createNode(const std::string&);

		/**
		 * Attaches a script to a node
		 * 
		 */
		void attachScript();

		/**
		 * Detaches a script from a node
		 *
		 */
		void detachScript();

	private:

		static inline uint32_t globalEntID_;

		// Scene tree
		editor::scene::SceneTree sceneTree_;

		// ECS
		ecs::ECSManager& ecs_;
	};
}
