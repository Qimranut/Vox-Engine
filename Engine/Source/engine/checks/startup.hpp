/*************************************************************************************************************
\file       startup.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares runStartup, which runs every startup self-check and optionally builds the physics
            demo arena.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once


namespace engine
{
	class SystemManager;
}


namespace engine::checks
{
	/*!
		@brief
			Runs the startup self-checks, then optionally builds the demo arena.
		@param systems
			Fully initialised subsystems
		@param spawnTestScene
			Also build the physics test scene - Game only, since in the Editor
			it would mix into whatever scene is open
		*/
	void runStartup(SystemManager& systems, bool spawnTestScene);
}
