/*************************************************************************************************************
\file       section_mesh.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      SectionMesh: the packed quads of one section at one LoD level, bucketed by face direction,
            plus the bounding box of the voxels that own them.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Voxel
#include "../types/voxel_types.hpp"
#include "../types/quad.hpp"

// stds
#include <cstdint>
#include <vector>


namespace voxel
{
	/**
	 * The meshed faces of one section at one LoD level, as the GPU wants
	 * them: packed quads grouped by face direction so a whole bucket can be
	 * skipped when the camera is behind it.
	 *
	 */
	struct SectionMesh
	{
		// Buckets 0..5 in Face order, concatenated.
		std::vector<Quad>	quads_;
		std::uint16_t		counts_[FaceCount]{};

		// minX | minY << 5 | minZ << 10 | (maxX-minX) << 15 | (maxY-minY) << 20 | (maxZ-minZ) << 25,
		// in this level's voxel units, covering every voxel that owns a face.
		std::uint32_t		aabb_{ 0 };

		/*!
			@brief
				Counts the quads across all six buckets.
			@return
				Total quad count
			*/
		std::uint32_t quadCount() const { return static_cast<std::uint32_t>(quads_.size()); }

		/*!
			@brief
				Reports whether the mesh has no quads.
			@return
				true when there are no quads
			*/
		bool empty() const { return quads_.empty(); }

		/*!
			@brief
				First quad of a bucket.
			@param face
				Face direction of the bucket
			@return
				Index into quads_ of the bucket's first quad
			*/
		std::uint32_t bucketOffset(int face) const
		{
			std::uint32_t o = 0;
			for (int f = 0; f < face; ++f)
				o += counts_[f];
			return o;
		}
	};
}
