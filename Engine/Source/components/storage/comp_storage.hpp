#pragma once

// Entt
#include "entt.hpp"


namespace cpn::storage
{
	/**
	 * Interface provides type-erasure for component storage
	 */
	class IComponentStorage
	{
	public:

		virtual ~IComponentStorage() = default;

		virtual void addDataToEntity(entt::registry&, entt::entity) const = 0;
	};

	template <typename T>
	class ComponentStorage : public IComponentStorage
	{
	public:

		ComponentStorage(const T& component)
			:
			component_ { component }
		{}

		/**
		 * Appends component data to entity.
		 * 
		 * [param] registry
		 * entt registry
		 * 
		 * [param] entity
		 * entt entity
		 * 
		 */
		void addDataToEntity(entt::registry& registry, entt::entity entity) const override
		{
			registry.emplace<T>(entity, component_);
		}

		T component_;
	};
}
