/*************************************************************************************************************
\file       scene_events.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Bus events published by SceneManager; currently SceneWorldChanged, sent when the scene's
            voxel world (.voxmap) reference changes.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// stds
#include <string>


namespace vox::events
{
	/**
	 * The scene's voxel world reference changed: a scene with a WORLD line
	 * was loaded, the Editor picked a map, or the scene was unloaded
	 * (empty path). Published by SceneManager on the main thread.
	 *
	 * The initial scene loads before the voxel system exists, so
	 * SystemManager replays the current value once after voxel init.
	 *
	 */
	struct SceneWorldChanged
	{
		std::string world_;		// asset-relative .voxmap path, or empty
	};
}
