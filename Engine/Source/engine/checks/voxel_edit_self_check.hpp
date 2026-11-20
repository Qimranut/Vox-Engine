/*************************************************************************************************************
\file       voxel_edit_self_check.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares the startup self-check for voxel editing: regions, grid raycast, brush ops, undo
            history and the batched LoD rebuild.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once


namespace engine::checks
{
	/*!
		@brief
			Exercises voxel editing's pure pieces at startup.

			Moving regions in and out of a world, the grid raycast, every brush op,
			the undo history and the batched LoD rebuild. Needs no window, physics
			world or ECS - none of it can be clicked in an automated run, so this
			is what guards it.

			Prints "[voxel-edit-check] all checks passed" or one FAILED line per
			broken invariant. Never throws.
		*/
	void runVoxelEditSelfCheck();
}
