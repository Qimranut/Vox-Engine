/*************************************************************************************************************
\file       mipper.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      The LoD collapse rule: mip8() picks one material for 2x2x2 voxels by LoD priority, and
            mipSection() builds a parent section from its eight children with it.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Voxel
#include "../types/voxel_types.hpp"
#include "../types/material_table.hpp"
#include "section.hpp"


namespace voxel
{
	/*!
		@brief
			Collapses 2x2x2 voxels into one.

			The material with the highest LoD priority wins, and equal priorities
			go to the highest child index (the +++ corner). Air never beats a
			solid. The priority comes from the material table, so the rule is
			data-driven.
		@param children
			Indexed by childIndex(x, y, z)
		@param materials
			The table the LoD priorities come from
		@return
			The winning material, or air when all eight are air
		*/
	inline MaterialId mip8(const MaterialId children[8], const MaterialTable& materials)
	{
		int best = -1;
		MaterialId out = kAir;

		for (int k = 0; k < 8; ++k)
		{
			if (children[k] == kAir)
				continue;

			const int key = (static_cast<int>(materials.get(children[k]).lodPriority_) << 3) | k;
			if (key > best)
			{
				best = key;
				out = children[k];
			}
		}

		return out;
	}

	/*!
		@brief
			Builds one parent section from its eight children (any may be
			absent - treated as air). Uniform children make the parent cheap.
		@param children
			Indexed by childIndex(cx, cy, cz); nullptr for children outside
			the world
		@param materials
			The table the LoD priorities come from
		@return
			The parent section, collapsed to uniform when possible
		*/
	Section mipSection(const Section* const children[8], const MaterialTable& materials);
}
