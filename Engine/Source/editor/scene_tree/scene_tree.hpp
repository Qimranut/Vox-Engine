#pragma once

// Node
#include "../node/data.hpp"

// stds
#include <vector>


namespace editor::scene
{
	class SceneTree
	{
	public:

		// Accessor
		std::vector<node::Data>& nodes()
		{
			return nodes_;
		}
		
	private:

		// Container for storing nodes
		std::vector<node::Data> nodes_;
	};
}
