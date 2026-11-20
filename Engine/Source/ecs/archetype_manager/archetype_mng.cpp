#include "archetype_mng.hpp"

namespace ecs
{
	Archetype& ArchetypeManager::createArchetype(const std::string& name)
	{
		auto archetype = std::make_unique<Archetype>(name);
		auto [it, inserted] = archetypes_.emplace(name, std::move(archetype));

		assert(inserted && "Archetype already exists.");

		return *it->second;
	}

	entt::entity ArchetypeManager::instantiate(entt::registry& registry, const Archetype& archetype)
	{
		// Create entity
		entt::entity entity = registry.create();

		// Append components to entity
		archetype.attachComponentsToEntity(registry, entity);

		// Log entity with added archetype
		instances_[&archetype].push_back(entity);

		return entity;
	}

	entt::entity ArchetypeManager::instantiate(entt::registry& registry, const std::string& archetypeName)
	{
		// Find out if archetype exists
		auto it = archetypes_.find(archetypeName);
		if (it == archetypes_.end())
			return entt::null;

		// Archetype exists

		// Create entity
		entt::entity entity = registry.create();

		// Append components to entity
		it->second->attachComponentsToEntity(registry, entity);

		// Log entity with added archetype
		instances_[it->second.get()].push_back(entity);

		return entity;
	}

	void ArchetypeManager::registerArchetypes()
	{
#define X_COMPONENT(type, value) archetype->addComponent<type>(value);

#define X(name, components)												\
		{																\
			auto archetype = std::make_unique<Archetype>(#name);		\
			components													\
			archetypes_[#name] = std::move(archetype);					\
		}																

		ARCHETYPE_LIST

#undef X
#undef X_COMPONENT
	}
}
