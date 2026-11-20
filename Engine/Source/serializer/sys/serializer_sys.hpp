#pragma once

// Component Registry
#include "../../components/list/comp_list.hpp"

// Physics types
#include "../../physics/types/physics_types.hpp"

// File IO util
#include "../../util/file_io/file_io.hpp"

// Directory
#include "../../directories/saves.hpp"

// glm
#include <glm/glm.hpp>

// entt
#include <entt.hpp>

// nlohmann
#include <nlohmann/json.hpp>

// stds
#include <string>
#include <fstream>


namespace serializer
{
	class System
	{
	public:

		void registerComponents();

		void loadAllSaves(entt::registry&);
		void save(entt::registry&, const std::string&);

	private:

		nlohmann::json load(entt::registry&, const std::string&);

		void saveEntity(entt::registry&, entt::entity, const std::string&);
		void loadEntity(entt::registry&, entt::entity, const nlohmann::json&);

		nlohmann::json serializeRegistry(entt::registry&);
		void loadRegistry(entt::registry&, const nlohmann::json&);

		nlohmann::json serializeEntity(entt::registry&, entt::entity);
		nlohmann::json toJson(entt::meta_any);
		entt::meta_any fromJson(entt::meta_type, const nlohmann::json&);

		template <typename MetaObj>
		std::string getName(const MetaObj& obj)
		{
			std::string_view name = obj.custom();
			return name.empty() ? "unnamed" : std::string{ name };
		}

		// ECS Dispatch helpers

		template <typename T>
		void addComponent(
			entt::registry& reg,
			entt::entity e,
			entt::meta_any& component)
		{
			reg.emplace_or_replace<T>(e, component.cast<T>());
		}

		void addComponentToRegistry(entt::registry&, entt::entity, entt::meta_type, entt::meta_any&);
	};
}
