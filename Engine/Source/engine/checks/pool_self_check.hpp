/*************************************************************************************************************
\file       pool_self_check.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares the startup self-check for util::memory::BlockPool and the voxel section pool.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once


namespace engine::checks
{
	/*!
		@brief
			Exercises util::memory::BlockPool and the voxel section pool built on it.

			Blocks are distinct, aligned and reused, the accounting returns to where
			it started, trim() hands empty slabs back and keeps used ones, the pool
			survives threads hammering it, and a Section's dense data really comes
			from the pool and goes back to it.

			Prints "[pool-check] all checks passed" or one FAILED line per broken
			invariant. Never throws.
		*/
	void runPoolSelfCheck();
}
