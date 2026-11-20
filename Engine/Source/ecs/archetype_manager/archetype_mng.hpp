#pragma once

// Archetype
#include "../archetype/archetype.hpp"

// Archetype list
#include "../../components/list/archetype_list.hpp"

// stds
#include <string>
#include <unordered_map>


namespace ecs
{
	class ArchetypeManager
	{
	public:

		/**
		 * Register archetypes listed in the archetype list on compile time
		 *
		 */
		void registerArchetypes();


		/**
		 * Create and store archetypes in the archetypes_ map.
		 *
		 * [param] name
		 * user-specified archetype name
		 *
		 * [return]
		 * Ref to archetype made/stored
		 *
		 */
		Archetype& createArchetype(const std::string&);

		/**
		 * Instantiates a new entt entity with a custom archetype and
		 * adds it to the entt registry.
		 *
		 * [param] reg
		 * entt registry
		 *
		 * [param] archetype
		 * Archetype to instantiate with
		 *
		 * [return]
		 * Instantiated entity
		 *
		 */
		entt::entity instantiate(entt::registry&, const Archetype&);

		/**
		 * Overload for instantiating a new entt entity. Instead of providing
		 * the archetype, the user provides the archetype name.
		 *
		 * [param] reg
		 * entt registry
		 *
		 * [param] archetype
		 * Archetype to instantiate with
		 *
		 * [return]
		 * Instantiated entity
		 *
		 */
		entt::entity instantiate(entt::registry&, const std::string&);

		/**
		 * Modify a component, and propogate the changes to all entities which share this component
		 * 
		 * [param] registry
		 * entt registry
		 * 
		 * [param] archetype
		 * archetype to modify
		 * 
		 * [param] component
		 * intended component changes
		 * 
		 */
		template <typename T>
		void modifyComponent(entt::registry& registry, Archetype& archetype, const T& component)
		{
			// Update archetype
			archetype.setComponent(component);

			// Find all entities created from passed archetype
			// If no entities are found with this archetype, 
			// return as no updating is required
			auto it = instances_.find(&archetype);
			if (it == instances_.end())
				return;

			// Update entities with archetype
			for (entt::entity ent : it->second)
			{
				registry.replace<T>(ent, component);
			}
		}

		/**
		 * Add a component to an archetype and 
		 * propogate the changes through every entity.
		 * 
		 * [param] registry
		 * entt registry
		 * 
		 * [param] archetype
		 * Archetype to be modified
		 * 
		 * [param] component
		 * Component to be added
		 * 
		 */
		template <typename T>
		void addComponent(entt::registry& registry, Archetype& archetype, const T& component)
		{
			// Update the archetype
			archetype.addComponent(component);

			// Find all entities with the provided archetype
			// Return if no such entities are found
			auto it = instances_.find(&archetype);
			if (it == instances_.end())
				return;

			// Update each entity to have this new component
			for (entt::entity ent : it->second)
			{
				registry.emplace<T>(ent, component);
			}
		}

		/**
		 * Remove a component from an archetype and
		 * propogate the changes through every entity.
		 *
		 * [param] registry
		 * entt registry
		 *
		 * [param] archetype
		 * Archetype to be modified
		 *
		 * [param] component
		 * Component to be added
		 *
		 */
		template <typename T>
		void removeComponent(entt::registry& registry, Archetype& archetype)
		{
			// Update the archetype
			archetype.removeComponent<T>();

			// Find all entities with the provided archetype
			// Return if no such entities are found
			auto it = instances_.find(&archetype);
			if (it == instances_.end())
				return;

			// Update each entity to have this new component
			for (entt::entity ent : it->second)
			{
				registry.remove<T>(ent);
			}
		}

	private:

		std::unordered_map<std::string, std::unique_ptr<Archetype>> archetypes_;

		std::unordered_map<const Archetype*, std::vector<entt::entity>> instances_;
	};
}
