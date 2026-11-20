#include "lua_interface.hpp"

namespace scripting::lua
{

	void Interface::init()
	{
		// Pure libraries only. string and table have no filesystem or
		// process access, and scripts cannot format a message or build a
		// list without them. io, os and package stay closed.
		lua_.open_libraries(sol::lib::base);
		lua_.open_libraries(sol::lib::math);
		lua_.open_libraries(sol::lib::string);
		lua_.open_libraries(sol::lib::table);
	}

	void Interface::reload()
	{
		// Clear previous script instances
		scriptsInstances_.clear();

		// Register existing script instances
		auto paths = util::file_io::lookForFiles(std::string(dir::script::files), ".lua");
		for (auto& p : paths)
		{
			scriptsInstances_.emplace(scriptInstanceID_++, LuaScript{ lua_, p.string() });
		}
	}

	void Interface::invokeScript(const std::string& scriptName)
	{
		std::string scriptPath = std::string(dir::script::files) + scriptName;
		lua_.script_file(scriptPath);
	}

	void Interface::invokeGlobal(const std::string& func)
	{
		for (auto& inst : scriptsInstances_)
		{
			inst.second.invoke(func);
		}
	}

	void Interface::retrieveVars()
	{
		for (auto& inst : scriptsInstances_)
		{
			inst.second.retrieve();
		}
	}

	sol::table Interface::createTable(const std::string& name)
	{
		return lua_.create_named_table(name);
	}

	bool Interface::createScript(const std::string& scriptName)
	{
		// Append extension if not provided
		std::string filename;
		if (!scriptName.ends_with(".lua"))
			filename = scriptName + ".lua";
		else
			filename = scriptName;

		// Create and open script
		std::ofstream newScript{ std::string{dir::script::files} + filename };
		if (newScript.is_open())
		{
			// Write template to file if file is open
			newScript << luaTemplate;
			newScript.close();

			// Store script instance
			scriptsInstances_.emplace(
				scriptInstanceID_++, 
				LuaScript{ lua_, std::string{ dir::script::files } + filename });

			std::cout << "New lua script has been successfully created." << std::endl;
			return true;
		}
		else
		{
			std::cerr << "Unable to create a new lua script." << std::endl;
			return false;
		}
	}
}
