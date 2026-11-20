/*************************************************************************************************************
\file       physics_test_scene.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares the builder for the Game-only physics demo arena.

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
			Builds a small scene to exercise physics by hand: ground, a wall
			for the spring arm, a box stack, a ramp, stairs, a trigger volume
			and the player.
		@param systems
			Fully initialised subsystems
		*/
	void buildPhysicsTestScene(SystemManager& systems);
}
