/*************************************************************************************************************
\file       hydrology.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares the water passes of the terrain prepass: priority-flood depression filling and
            lake finding, which levels erosion pits and fills real basins with water.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Voxel
#include "heightfield.hpp"
#include "recipe.hpp"


namespace voxel
{
	/** Value of a water-level cell that holds no water. */
	inline constexpr float kNoWater = -1.0e9f;

	/*!
		@brief
			Priority-flood depression filling (Barnes, Lehman & Mulla 2014).

			Floods inward from the map edge, lowest cell first, so every cell
			ends at the height water would need to reach to drain off the map.
			Where that is above the ground there is a depression.

			The queue is ordered by (height, cell index), a total order, so the
			result does not depend on how the heap breaks ties.
		@param field
			Ground heights
		@return
			The filled surface, never below field
		*/
	HeightField fillDepressions(const HeightField& field);

	/*!
		@brief
			Turns depressions into lakes.

			Each connected depression with one spill height is one candidate:
			those too shallow or too small are erosion pits and are filled with
			ground instead; the rest hold water up to a share of their depth,
			capped so an enclosed arena floor does not drown.
		@param field
			Pits are filled in place
		@param params
			Water settings from the recipe
		@return
			Water surface height per cell in metres, kNoWater where dry
		*/
	HeightField findLakes(HeightField& field, const Recipe::Water& params);
}
