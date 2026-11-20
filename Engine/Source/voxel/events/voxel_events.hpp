/*************************************************************************************************************
\file       voxel_events.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Bus events the voxel system publishes: WorldLoaded, WorldUnloaded, WorldReady and
            SectionChanged.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// GLM
#include <glm/glm.hpp>

// stds
#include <string>


namespace voxel::events
{
	/**
	 * A world's voxel data is installed: queries work, meshes and bodies
	 * are still streaming in.
	 *
	 */
	struct WorldLoaded
	{
		std::string	map_;			// asset-relative path, or the recipe path for a fresh generate
		glm::ivec3	sizeVoxels_;
	};

	struct WorldUnloaded {};

	/**
	 * Every mesh is resident and every level-0 collision body is in the
	 * physics world. Safe to drop the player in.
	 *
	 */
	struct WorldReady {};

	/**
	 * A level-0 section was edited and has been remeshed.
	 *
	 */
	struct SectionChanged
	{
		glm::ivec3 section_;
	};
}
