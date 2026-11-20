#pragma once

// Type
#include "../types/lua_types.hpp"

// Script instance base
#include "../../scripting/script_interface.hpp"

// Profiling
#include "../../profile/profiler.hpp"

// Lua
#include <sol/sol.hpp>

// stds
#include <string>
#include <unordered_map>
#include <any>


namespace scripting::lua
{
	struct Variable
	{
		std::any value_;
		VariableType type_;
	};

	class LuaScript : public IScript
	{
	public:

		LuaScript(sol::state& lua, const std::string& path)
			:
			path_{ path },
			env_{ lua, sol::create, lua.globals() }
		{
			VOX_PROFILE_SCOPE_C("Lua load script", profile::colour::kScript);
			VOX_PROFILE_TEXT(path_.data(), path_.size());

			lua.script_file(path, env_);
		}
		
		/**
		 * Invokes a function from the script instance
		 * 
		 * [param] func
		 * Function to invoke
		 * 
		 */
		void invoke(const std::string& func)
		{
			VOX_PROFILE_SCOPE_C("Lua invoke", profile::colour::kScript);
			VOX_PROFILE_TEXT(func.data(), func.size());
			VOX_PROFILE_TEXT(path_.data(), path_.size());

			sol::object function = env_[func];
			if (!function.valid())
			{
				std::cerr << "Failed to invoke the function: " << func << std::endl <<
					"for the script: " << path_ << std::endl;
				return;
			}

			if (function.get_type() != sol::type::function)
			{
				std::cerr << "Attempted to invoke a non-function. This is an invalid operation." << std::endl;
				return;
			}

			sol::function luaF = function.as<sol::function>();
			luaF();
		}

		/**
		 * Retrieves any variables from the script instance, and maps them
		 *
		 */
		void retrieve()
		{
			for (auto& entry : env_)
			{
				std::string key = entry.first.as<std::string>();
				sol::object value = entry.second;
				
				Variable var;

				if (value.get_type() == sol::type::lua_nil)
				{
					var.type_ = VariableType::Nil;
					var.value_ = nullptr;
				}
				else if (value.is<double>())
				{
					var.type_ = VariableType::Double;
					var.value_ = value.as<double>();
				}
				else if (value.is<bool>())
				{
					var.type_ = VariableType::Bool;
					var.value_ = value.as<bool>();
				}
				else if (value.is<std::string>())
				{
					var.type_ = VariableType::String;
					var.value_ = value.as<std::string>();
				}
				else
					continue;

				vars_[key] = var;
			}
		}

		/**
		 * Modify a script variable
		 * 
		 * [param] varToMod
		 * Variable to modify
		 * 
		 * [param] newValue
		 * New value for the modded variable
		 * 
		 */
		bool modifyVar(const std::string& var, const std::any& newValue)
		{
			auto it = vars_.find(var);
			if (it == vars_.end())
			{
				std::cerr << "Variable to modify could not be found.";
				return false;
			}

			switch (it->second.type_)
			{
			case VariableType::Nil:
				return false;
			case VariableType::Double:
				if (newValue.type() == typeid(double))
				{
					it->second.value_ = newValue;
					env_[var] = std::any_cast<double>(newValue);
					return true;
				}

				break;
			case VariableType::Bool:
				if (newValue.type() == typeid(bool))
				{
					it->second.value_ = newValue;
					env_[var] = std::any_cast<bool>(newValue);
					return true;
				}

				break;
			case VariableType::String:
				if (newValue.type() == typeid(std::string))
				{
					it->second.value_ = newValue;
					env_[var] = std::any_cast<std::string>(newValue);
					return true;
				}

				break;
			default:
				std::cerr << "Unknown type detected." << std::endl;
				return false;
			}

			std::cerr << "New variable type is not the same as the replaced variable." << std::endl;
			return false;
		}

	private:

		sol::environment env_;
		std::string path_;

		std::unordered_map<std::string, Variable> vars_;
	};
}
