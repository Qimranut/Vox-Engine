/*************************************************************************************************************
\file       voxel_types.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Compile-time voxel constants (section size, LoD levels, material ids) and the small index
            helpers shared by the whole voxel module: voxel and child indices, faces and their normals.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// GLM
#include <glm/glm.hpp>

// stds
#include <cstddef>
#include <cstdint>


namespace voxel
{
	// ---------------------------------------------------------------------
	//  Compile-time constants. Everything else about a world (voxel size,
	//  extent, materials, generation) is data.
	//
	//  32 is not negotiable: the quad format stores coordinates in 5 bits,
	//  and the mesher's row masks are 32-bit words.
	// ---------------------------------------------------------------------

	inline constexpr int	kSectionSize	= 32;
	inline constexpr int	kSectionShift	= 5;
	inline constexpr int	kSectionMask	= kSectionSize - 1;
	inline constexpr int	kSectionVolume	= kSectionSize * kSectionSize * kSectionSize;

	// Levels 0..3: a level-L section covers 32 << L voxels per axis.
	inline constexpr int	kLodLevels		= 4;

	// Runtime material id. 0 is always air. Files store ids by name.
	using MaterialId = std::uint8_t;

	inline constexpr MaterialId kAir				= 0;
	inline constexpr MaterialId kUnknownMaterial	= 0xFF;

	/*!
		@brief
			Voxel index inside a section: Y-major, then Z, then X.

			The X run is contiguous so a row of 32 voxels maps onto one 32-bit mask.
		@param x
			Voxel x (only the low 5 bits are used)
		@param y
			Voxel y (only the low 5 bits are used)
		@param z
			Voxel z (only the low 5 bits are used)
		@return
			The index, 0..32767
		*/
	inline constexpr std::size_t voxelIndex(int x, int y, int z)
	{
		return static_cast<std::size_t>(((y & kSectionMask) << 10) | ((z & kSectionMask) << 5) | (x & kSectionMask));
	}

	/*!
		@brief
			Child slot of a voxel or section inside its parent: x in bit 2, y in
			bit 1, z in bit 0.

			Index 7 is the +++ corner, which the LoD rule prefers on ties.
		@param x
			Child x (only the low bit is used)
		@param y
			Child y (only the low bit is used)
		@param z
			Child z (only the low bit is used)
		@return
			The slot, 0..7
		*/
	inline constexpr int childIndex(int x, int y, int z)
	{
		return ((x & 1) << 2) | ((y & 1) << 1) | (z & 1);
	}

	/**
	 * Faces as the quad format encodes them: axis << 1 | side. Axis 0 is
	 * Y, 1 is Z, 2 is X (not vec3 index order); side 0 faces -, side 1 faces +.
	 *
	 */
	enum Face : std::uint8_t
	{
		FaceNegY = 0, FacePosY = 1,
		FaceNegZ = 2, FacePosZ = 3,
		FaceNegX = 4, FacePosX = 5,
		FaceCount = 6
	};

	/*!
		@brief
			The axis of a face code (0 = Y, 1 = Z, 2 = X).
		@param face
			The face code
		@return
			The axis
		*/
	inline constexpr int faceAxis(int face) { return face >> 1; }

	/*!
		@brief
			The side of a face code (0 faces -, 1 faces +).
		@param face
			The face code
		@return
			The side
		*/
	inline constexpr int faceSide(int face) { return face & 1; }

	/*!
		@brief
			Unit normal of a face, in world axes.
		@param face
			The face code
		@return
			The normal
		*/
	inline constexpr glm::ivec3 faceNormal(int face)
	{
		const int s = faceSide(face) ? 1 : -1;
		switch (faceAxis(face))
		{
		case 0:  return { 0, s, 0 };
		case 1:  return { 0, 0, s };
		default: return { s, 0, 0 };
		}
	}
}
