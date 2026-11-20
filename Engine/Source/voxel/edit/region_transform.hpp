/*************************************************************************************************************
\file       region_transform.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Placement of a copied block of voxels (move, turn, scale about its centre) and place(),
            which writes the placed copy into a target region.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Voxel
#include "voxel_region.hpp"

// GLM
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>


namespace voxel::edit
{
	/**
	 * Where a copied block of voxels is put back: moved, turned and scaled
	 * about its own centre. In voxel units, like everything in edit/.
	 *
	 */
	struct Placement
	{
		glm::vec3	centre_		{ 0.f };					// of the placed block, in the voxel grid
		glm::quat	rotation_	{ 1.f, 0.f, 0.f, 0.f };
		glm::vec3	scale_		{ 1.f };					// placed size over source size, per source axis

		/*!
			@brief
				The placement that puts a block back exactly where it came from.
			@param source
				The block's source box
			@return
				A placement centred on the box with no turn and unit scale
			*/
		static Placement identityFor(const VoxelBox& source)
		{
			Placement p;
			p.centre_ = 0.5f * glm::vec3(source.min_ + source.max_);
			return p;
		}
	};

	/*!
		@brief
			Every voxel a placed block can reach.
		@param source
			The block's source box
		@param placement
			Where and how the block is placed
		@return
			The bounding voxel box
		*/
	VoxelBox placedBounds(const VoxelBox& source, const Placement& placement);

	/*!
		@brief
			Writes a placed copy of source into target.

			Each target voxel looks up the source voxel its centre maps back to
			(nearest neighbour, so materials stay what they were and a quarter
			turn is exact).
		@param target
			Region to write into
		@param source
			The copied block
		@param placement
			Where and how the block is placed
		@param mergeEmpty
			When set, empty voxels of the block leave the target alone, so the
			block merges into what is there; otherwise they clear it, and the block
			arrives with its own air
		@return
			Voxels changed
		*/
	std::size_t place(VoxelRegion& target, const VoxelRegion& source, const Placement& placement, bool mergeEmpty);
}
