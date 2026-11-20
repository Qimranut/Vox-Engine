#pragma once

// Script instance
#include "../script_instance/lua_instance.hpp"

// Directories
#include "../../directories/script.hpp"	

// File IO utility
#include "../../util/file_io/file_io.hpp"

// Sol
#include <sol/sol.hpp>

// stds
#include <string>
#include <iostream>
#include <vector>


namespace scripting::lua
{
	class Interface
	{
	public:

		/**
		 * Initializes lua interface,
		 * by opening available libs
		 *
		 */
		void init();

		/**
		 * Clear previous script instances 
		 * and looks for existing scripts to form new instances
		 * 
		 */
		void reload();

		/**
		 * Invokes a lua script by name
		 * 
		 * [param] 
		 */
		void invokeScript(const std::string&);

		/**
		 * Invokes all scripts' provided function
		 * 
		 * [param]
		 * Function to invoke
		 */
		void invokeGlobal(const std::string&);

		/**
		 * Retrieve Lua script vars
		 * 
		 */
		void retrieveVars();

		/**
		 * Creates and returns a named table.
		 *
		 * [param]
		 * Named table name
		 *
		 * [return]
		 * Named table
		 *
		 */
		sol::table createTable(const std::string&);

		/**
		 * Creates a lua script.
		 *
		 * [param]
		 * Script name
		 * 
		 * [return] 
		 * Script process success code
		 * 
		 */
		bool createScript(const std::string&);

		/**
		 * Creates a table and specifies a new function to expose in lua.
		 * 
		 * [param] tableName
		 * Table to initialize
		 * 
		 * [param] funcName
		 * Name of function to expose in lua
		 * 
		 * [param] func
		 * Function to connect
		 * 
		 */
		template <typename Function>
		void link(const std::string& tableName, const std::string& funcName, Function func)
		{
			sol::optional<sol::table> table = lua_.get<sol::optional<sol::table>>(tableName);

			if (table)
			{
				(*table)[funcName] = func;
			}
			else
			{
				sol::table newTable = lua_.create_named_table(tableName);
				newTable[funcName] = func;
			}
		}

		/**
		 * Creates a table and specifies a new function to expose in lua.
		 *
		 * [param] tableName
		 * Table to initialize
		 *
		 * [param] funcName
		 * Name of function to expose in lua
		 *
		 * [param] func
		 * Function to connect
		 *
		 */
		template <typename... Functions>
		void linkOverload(const std::string& tableName, const std::string& funcName, Functions... func)
		{
			sol::optional<sol::table> table = lua_.get<sol::optional<sol::table>>(tableName);

			auto overload = sol::overload(func...);

			if (table)
			{
				(*table)[funcName] = overload;
			}
			else
			{
				sol::table newTable = lua_.create_named_table(tableName);
				newTable[funcName] = overload;
			}
		}

	private:

		sol::state lua_;

		std::unordered_map<uint32_t, LuaScript> scriptsInstances_;

		std::string luaTemplate =
			"function OnCreate()\n"
			"\n"
			"end\n"
			"\n"
			"function OnUpdate()\n"
			"\n"
			"end\n"
			"\n"
			"function OnDestroy()\n"
			"\n"
			"end\n";

		static inline uint32_t scriptInstanceID_ = 0;
	};
}
