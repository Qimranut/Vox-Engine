#pragma once

// Component list
#include "../list/comp_list.hpp"

// Archetype list
#include "../list/archetype_list.hpp"

// stds
#include <typeindex>
#include <unordered_map>


namespace cpn::backend
{
	/*class Registry
	{
	private:

		struct ComponentInfo
		{
			uint32_t id_;
			size_t size_;
			size_t alignment_;
		};

		static std::unordered_map<std::type_index, ComponentInfo>& ComponentMap()
		{
			static std::unordered_map<std::type_index, ComponentInfo> map
			{
#define X(Type) { std::type_index(typeid(Type)), {__COUNTER__, sizeof(Type), alignof(Type) }},
				COMPONENT_LIST
#undef X
			};

			return map;
		}

	public:

		template <typename Component>
		static void Register()
		{
			auto index = std::type_index(typeid(Component));
			auto it = ComponentMap().find(index);
			if (it != ComponentMap().end())
			{
				throw std::runtime_error(std::string(index.name())
					+ " has already been registered in the component map.");
			}

			size_t id = ComponentMap().size();
			ComponentMap().emplace(
				index, 
				ComponentInfo{ static_cast<uint32_t>(id), sizeof(Component), alignof(Component) });
		}
	};*/
}
