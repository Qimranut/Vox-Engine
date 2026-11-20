/*************************************************************************************************************
\file       section_mesher.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares the section mesher: gathering a section with its neighbours and turning it into
            greedy-merged, direction-bucketed quads.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Voxel
#include "../types/voxel_types.hpp"
#include "../types/material_table.hpp"
#include "../world/section.hpp"
#include "section_mesh.hpp"

// GLM
#include <glm/glm.hpp>


namespace voxel
{
	class World;

	/**
	 * What one meshing job reads: the section and its six face neighbours
	 * at the same LoD level. Null neighbours are outside the world (air).
	 * Everything is read-only, so jobs need no locks while the world is
	 * not being edited.
	 *
	 * sealBorders_ keeps every face on the section's six outer planes, even
	 * where the same-level neighbour is solid. Coarse levels need it: next
	 * to a finer node the neighbour that would have hidden the face is not
	 * what is drawn, and because a mip is always a superset of its children
	 * (air never beats a solid) the coarse side is the one left open. The
	 * walls close that crack; between two nodes of the same level they are
	 * buried and cost a handful of merged quads.
	 *
	 */
	struct MeshInput
	{
		const Section*	self_{ nullptr };
		const Section*	neighbours_[FaceCount]{};	// indexed by Face
		bool			sealBorders_{ false };
	};

	/*!
		@brief
			Gathers the input for a section at a level.

			Levels above 0 are gathered with sealBorders_ set.
		@param world
			World to read from
		@param level
			LoD level
		@param coord
			Section coordinate at that level
		@return
			The section and its six face neighbours (null outside the world)
		*/
	MeshInput gatherMeshInput(const World& world, int level, const glm::ivec3& coord);

	/*!
		@brief
			Turns a section into quads.

			Faces are found with a row-mask trick: one 32-bit word of opacity
			per (y, z) row, XOR of adjacent rows gives every visible face along
			that axis in one op. Faces then pass through the greedy plane
			mesher, bucketed by direction.
		@param in
			The section and its neighbours
		@param materials
			Material table, for opacity
		@param ao
			Bake 2-bit corner ambient occlusion into every quad. Off, every
			quad carries AO 0xFF so merging is maximal.
		@return
			The mesh; empty for uniform air or a fully enclosed section
		*/
	SectionMesh meshSection(const MeshInput& in, const MaterialTable& materials, bool ao);
}
