/*************************************************************************************************************
\file       voxel_world.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      cpn::VoxelWorld, the runtime entity of a loaded voxel world, which owns every terrain
            collision body.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// stds
#include <string>


namespace cpn
{
	/**
	 * The runtime entity of a loaded voxel world. One exists while a map
	 * is loaded; every terrain collision body carries it as owner, so a
	 * physics raycast that hits terrain reports this entity.
	 *
	 * Not serialized: the scene's WORLD line is the source of truth, and
	 * voxel::System creates and destroys this entity itself.
	 *
	 */
	struct VoxelWorld
	{
		std::string map_;		// asset-relative .voxmap path
	};
}
