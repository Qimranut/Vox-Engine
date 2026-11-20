/*************************************************************************************************************
\file       character_body.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      cpn::CharacterBody, the component for a capsule character driven by Jolt's CharacterVirtual:
            its authoring values (size, speeds, slope, player control) and runtime handle.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// stds
#include <cstdint>


namespace cpn
{
	/**
	 * A capsule character driven by Jolt's CharacterVirtual.
	 *
	 * Needs a cpn::Transform. Created lazily by physics::System like
	 * cpn::RigidBody. Transform::pos_ is the capsule CENTRE and is written by
	 * physics every frame; the character's feet are height_ / 2 below it.
	 *
	 */
	struct CharacterBody
	{
		// ------------------------- Authoring --------------------------

		float			radius_				{ 0.3f };
		float			height_				{ 1.8f };	// total, caps included

		float			walkSpeed_			{ 4.5f };	// m/s
		float			sprintSpeed_		{ 7.5f };	// m/s
		float			jumpSpeed_			{ 5.5f };	// m/s, initial upward velocity

		float			groundAccel_		{ 12.f };	// 1/s, how fast velocity reaches the target on the ground
		float			airControl_			{ 0.3f };	// fraction of groundAccel_ available in the air

		float			maxSlopeDeg_		{ 50.f };

		// Receives the engine's Move / Sprint / Jump intent. Only one should.
		bool			playerControlled_	{ false };

		// -------------------------- Runtime ---------------------------

		std::uint32_t	handle_				{ 0xffffffff };
	};
}
