/*************************************************************************************************************
\file       voxel_math.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Conversions between world space and the voxel grid: voxel corners, the voxel containing a
            point, the voxel behind a surface hit, and section bounds.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Voxel
#include "../types/voxel_types.hpp"
#include "world.hpp"

// GLM
#include <glm/glm.hpp>


namespace voxel
{
	/*!
		@brief
			World-space position of a voxel's minimum corner.
		@param dims
			The world's dimensions
		@param v
			The voxel
		@return
			The corner position
		*/
	inline glm::vec3 voxelToWorld(const WorldDims& dims, const glm::ivec3& v)
	{
		return dims.origin_ + glm::vec3(v) * dims.voxelSize_;
	}

	/*!
		@brief
			Voxel containing a world-space point (may be outside the grid).
		@param dims
			The world's dimensions
		@param p
			World-space point
		@return
			The voxel
		*/
	inline glm::ivec3 worldToVoxel(const WorldDims& dims, const glm::vec3& p)
	{
		return glm::ivec3(glm::floor((p - dims.origin_) / dims.voxelSize_));
	}

	/*!
		@brief
			The solid voxel behind a surface hit.

			Steps half a voxel against the hit normal so points exactly on a face
			resolve into the solid side.
		@param dims
			The world's dimensions
		@param point
			World-space hit point
		@param normal
			Surface normal at the hit
		@return
			The voxel
		*/
	inline glm::ivec3 hitToVoxel(const WorldDims& dims, const glm::vec3& point, const glm::vec3& normal)
	{
		const glm::vec3 p = (point - dims.origin_) / dims.voxelSize_ - normal * 0.5f;
		return glm::ivec3(glm::floor(p));
	}

	/*!
		@brief
			World-space bounds of a section at a LoD level.
		@param dims
			The world's dimensions
		@param level
			LoD level
		@param s
			Section coordinate at that level
		@param mn
			Receives the minimum corner
		@param mx
			Receives the maximum corner
		*/
	inline void sectionBounds(const WorldDims& dims, int level, const glm::ivec3& s, glm::vec3& mn, glm::vec3& mx)
	{
		const float edge = static_cast<float>(kSectionSize << level) * dims.voxelSize_;
		mn = dims.origin_ + glm::vec3(s) * edge;
		mx = mn + glm::vec3(edge);
	}
}
