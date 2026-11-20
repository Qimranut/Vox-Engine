#pragma once

// Script interface
#include "../../scripting/script_interface.hpp"

// Entt
#include "entt.hpp"

// stds
#include <memory>


namespace editor::node
{
	struct Data
	{
		uint32_t								id_;
		entt::entity							ent_;
	};
}
