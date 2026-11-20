#include "editor_sys.hpp"

namespace editor
{
	System::System(ecs::ECSManager& ecs)
		:
		ecs_				{ ecs }
	{}

	void System::createNode()
	{
		node::Data node;
		node.id_ = globalEntID_++;
		node.ent_ = ecs_.createEntity();

		sceneTree_.nodes().push_back(node);
	}

	void System::createNode(ecs::Archetype& archetype)
	{
		node::Data node;
		node.id_ = globalEntID_++;
		node.ent_ = ecs_.instantiate(archetype);

		sceneTree_.nodes().push_back(node);
	}

	void System::createNode(const std::string& archetypeName)
	{
		node::Data node;
		node.id_ = globalEntID_++;
		node.ent_ = ecs_.instantiate(archetypeName);

		sceneTree_.nodes().push_back(node);
	}

	void System::attachScript()
	{

	}

	void System::detachScript()
	{

	}
}
