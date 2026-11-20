#pragma once

// Archetype
#include "../archetype_manager/archetype_mng.hpp"

// Entt
#include <entt.hpp>


namespace ecs
{
	class ECSManager
	{
	public:

		void registerArchetypes()
		{
			archetypeMng_.registerArchetypes();
		}

		// Utility

		/**
		 * Create an entity without components
		 * 
		 * [return] 
		 * Newly created entity
		 * 
		 */
		entt::entity createEntity()
		{
			return registry_.create();
		}

		/**
		 * Create an entity without components
		 * from an existing entity
		 *
		 * [return]
		 * Newly created entity
		 *
		 */
		entt::entity createEntity(entt::entity e)
		{
			return registry_.create(e);
		}


		/**
		 * Creates an archetype with a custom name
		 * 
		 * [param] 
		 * User-passed archetype name
		 * 
		 * [return] 
		 * Reference to the newly created archetype
		 * 
		 */
		Archetype& createArchetype(const std::string& name)
		{
			return archetypeMng_.createArchetype(name);
		}

		/**
		 * Instantiates an entity out of the provided archetype.
		 * 
		 * [param] 
		 * Archetype to create an entity out of
		 * 
		 * [return] 
		 * Instantiated entity
		 * 
		 */
		entt::entity instantiate(const Archetype& archetype)
		{
			return archetypeMng_.instantiate(registry_, archetype);
		}

		/**
		 * Instantiates an entity out of a registered archetype, by name.
		 *
		 * Without this overload a string silently converts to a TEMPORARY
		 * Archetype through its converting constructor - one with no
		 * components that the archetype manager never tracks.
		 *
		 * [param] archetypeName
		 * Name as spelled in ARCHETYPE_LIST
		 *
		 * [return]
		 * Instantiated entity, or entt::null when no such archetype exists
		 *
		 */
		entt::entity instantiate(const std::string& archetypeName)
		{
			return archetypeMng_.instantiate(registry_, archetypeName);
		}

		/**
		 * Destroys an entity and every component on it.
		 *
		 * [param] entity
		 * Entity to destroy. Invalid entities are ignored.
		 *
		 */
		void destroyEntity(entt::entity entity)
		{
			if (registry_.valid(entity))
				registry_.destroy(entity);
		}

		/**
		 * Modifies an archetype component, 
		 * and modifies the value of every entity with the current archetype
		 * 
		 * [param] archetype
		 * Archetype to modify
		 * 
		 * [param] component
		 * Component to modify
		 * 
		 */
		template <typename T>
		void modifyArchetypeComponent(Archetype& archetype, const T& component)
		{
			archetypeMng_.modifyComponent(registry_, archetype, component);
		}

		/**
		 * Add a component to an archetype and update all registered entities
		 * 
		 * [param] archetype
		 * Archetype to modify
		 * 
		 * [param] component
		 * Component to add
		 * 
		 */
		template <typename T>
		void addArchetypeComponent(Archetype& archetype, const T& component)
		{
			archetypeMng_.addComponent(registry_, archetype, component);
		}

		/**
		 * Remove a component from an archetype and update all registered entities
		 *
		 * [param] archetype
		 * Archetype to modify
		 *
		 * [param] component
		 * Component to remove
		 *
		 */
		template <typename T>
		void removeArchetypeComponent(Archetype& archetype)
		{
			archetypeMng_.removeComponent<T>(registry_, archetype);
		}

		// Accessor

		entt::registry& registry()
		{
			return registry_;
		}

		const entt::registry& registry() const
		{
			return registry_;
		}

	private:

		entt::registry registry_;

		ArchetypeManager archetypeMng_;
	};
}
