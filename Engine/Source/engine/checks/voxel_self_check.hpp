/*************************************************************************************************************
\file       voxel_self_check.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares the startup self-check for the voxel pipeline's pure pieces (quad packing, mips,
            mesher, map codec, noise and generation).

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once


namespace engine::checks
{
	/*!
		@brief
			Exercises the voxel pipeline's pure pieces at startup.

			Quad packing, the LoD mip rule, the greedy mesher, the map codec and
			the noise. Needs no window, physics world or ECS.

			Prints "[voxel-check] all checks passed" or one FAILED line per
			broken invariant. Never throws.
		*/
	void runVoxelSelfCheck();
}
