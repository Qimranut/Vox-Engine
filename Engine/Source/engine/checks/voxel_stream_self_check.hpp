/*************************************************************************************************************
\file       voxel_stream_self_check.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares the startup self-check for endless terrain streaming (tiles, seams, cell builds,
            kept edits and the endless recipe).

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once


namespace engine::checks
{
	/*!
		@brief
			Exercises terrain streaming's pure pieces at startup, with no window,
			physics world or player.

			Checks that a tile of an endless terrain is a function of (recipe, tile)
			alone, that two tiles side by side computed the same ground along their
			seam, that a cell built on a worker has the LoD levels a World would
			have built for it, whatever detail was asked for, that a hand edit kept
			from an evicted cell comes back in every level, and that an endless
			recipe survives its text and is refused by the whole-map generator.

			Prints "[voxel-stream-check] all checks passed" or one FAILED line per
			broken invariant. Never throws.
		*/
	void runVoxelStreamSelfCheck();
}
