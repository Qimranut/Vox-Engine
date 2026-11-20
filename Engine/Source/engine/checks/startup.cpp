/*************************************************************************************************************
\file       startup.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Runs every startup self-check in order (input, physics, texture, pool, voxel, voxel edit,
            voxel stream, profile) and, in Game only, builds the physics test scene.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "startup.hpp"

// Engine
#include "../systems/system_manager.hpp"
#include "input_self_check.hpp"
#include "physics_self_check.hpp"
#include "physics_test_scene.hpp"
#include "pool_self_check.hpp"
#include "voxel_self_check.hpp"
#include "voxel_edit_self_check.hpp"
#include "voxel_stream_self_check.hpp"

// The texture checks live beside the texture code they test
#include "../../graphics/texture/texture_self_check.hpp"

// Likewise the frame statistics
#include "../../profile/profile_self_check.hpp"
#include "../../profile/profiler.hpp"


namespace engine::checks
{
	/*!
		@brief
			Runs the startup self-checks, then optionally builds the demo arena.

			The pool check runs ahead of the voxel checks, because every dense
			section they make comes from the pool.
		@param systems
			Fully initialised subsystems
		@param spawnTestScene
			Also build the physics test scene (Game only)
		*/
	void runStartup(SystemManager& systems, bool spawnTestScene)
	{
		VOX_PROFILE_SCOPE("Startup checks");

		runInputSelfCheck();

		runPhysicsSelfCheck(systems.physicsSys(), systems.ecsManager(), systems.bus());

		graphics::texture::runSelfCheck(systems.resources());

		// Ahead of the voxel checks: every dense section they make comes from the pool.
		runPoolSelfCheck();

		runVoxelSelfCheck();

		runVoxelEditSelfCheck();

		runVoxelStreamSelfCheck();

		profile::runSelfCheck();

		if (spawnTestScene)
			buildPhysicsTestScene(systems);
	}
}
