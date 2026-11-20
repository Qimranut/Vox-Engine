#pragma once

// Component storage
#include "../../components/storage/comp_storage.hpp"

// Entt
#include "entt.hpp"

// stds
#include <vector>
#include <memory>
#include <typeindex>
#include <unordered_map>


namespace ecs
{
	class Archetype
	{
	public:

		Archetype(const std::string& name)
			:
			name_{ name }
		{}

		// Utility

		/**
		 * Helper for adding each component in the archetype to the entity in the registry
		 * 
		 * [param] registry
		 * entt registry
		 * 
		 * [param] entity
		 * entt entity
		 * 
		 */
		void attachComponentsToEntity(entt::registry& registry, entt::entity entity) const
		{
			for (const auto& [type, storage] : components_)
			{
				storage->addDataToEntity(registry, entity);
			}
		}

		/**
		 * Templated utility function to
		 * retrieve the component stored in the archetype
		 *
		 * [param] component
		 * Component requested
		 *
		 */
		template <typename T>
		T& getComponent()
		{
			auto it = components_.find(std::type_index(typeid(T)));
			assert(it != components_.end() && "Component does not exist in the archetype.");

			auto* storage = static_cast<cpn::storage::ComponentStorage<T>*>(it->second.get());

			return storage->component_;
		}

		/**
		 * Templated utility function to
		 * add a component to the archetype
		 *
		 * [param] component
		 *
		 */
		template <typename T>
		void addComponent(const T& component)
		{
			auto [it, inserted] = components_.emplace(
				std::type_index(typeid(T)),
				std::make_unique<cpn::storage::ComponentStorage<T>>(component));

			assert(inserted && "Component already exists in archetype");
		}

		/**
		 * Templated utility function to
		 * set new values to a component in the archetype
		 *
		 * [param] component
		 *
		 */
		template <typename T>
		void setComponent(const T& component)
		{
			auto it = components_.find(std::type_index(typeid(T)));
			assert(it != components_.end() && "Component does not exist in archetype.");

			auto* storage = static_cast<cpn::storage::ComponentStorage<T>*>(it->second.get());

			storage->component_ = component;
		}

		/**
		 * Templated utility function to
		 * remove a component from an archetype
		 *
		 * [param] component
		 *
		 */
		template <typename T>
		void removeComponent()
		{
			auto it = components_.find(std::type_index(typeid(T)));
			assert(it != components_.end() && "Component does not exist in archetype.");

			components_.erase(it);
		}

		// Accessor

		/**
		 * Accessor for getting the archetype name
		 *
		 * [return]
		 * Archetype name
		 */
		const std::string& GetName() const
		{
			return name_;
		}

	private:

		std::string name_;

		std::unordered_map<std::type_index, std::unique_ptr<cpn::storage::IComponentStorage>> components_;
	};
}
