/*************************************************************************************************************
\file       draw_types.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      GPU-facing records of the voxel renderer: the per-section instance record, the indirect
            draw command, and the packing of section coordinates and LoD into 32 bits.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// stds
#include <cstdint>


namespace voxel::render
{
	/**
	 * One per section drawn this frame. Bound as one instanced uvec4
	 * vertex attribute (divisor 1); a draw's baseInstance picks its
	 * record, which keeps the shader on core GL 4.5 without
	 * gl_BaseInstance. Only the first 16 bytes reach the shader.
	 *
	 */
	struct SectionRecord
	{
		std::uint32_t	posLod_;			// x:10 | y:8 << 10 | z:10 << 18 | lod:3 << 28, section coords at that lod
		std::uint32_t	aabb_;				// SectionMesh::aabb_
		std::uint32_t	geometryOffset_;	// first quad of the section in the arena
		std::uint32_t	flags_;				// reserved
		std::uint16_t	counts_[6];			// per-face bucket sizes, for stats and tools
		std::uint32_t	pad_;
	};

	static_assert(sizeof(SectionRecord) == 32);

	/**
	 * GL DrawElementsIndirectCommand.
	 *
	 */
	struct DrawCommand
	{
		std::uint32_t	count_;				// indices: quads * 6
		std::uint32_t	instanceCount_;		// 1
		std::uint32_t	firstIndex_;		// 0
		std::int32_t	baseVertex_;		// absolute quad index * 4
		std::uint32_t	baseInstance_;		// record index
	};

	static_assert(sizeof(DrawCommand) == 20);

	// What packPosLod (and VoxelQuad.vert, which unpacks it) has room for: a
	// section's coordinates in 10 / 8 / 10 bits. 1024 sections of 32 voxels is
	// 32768 voxels a side, 3.2 km at 10 cm - the ceiling on a streamed world's
	// extent (Recipe::parse enforces it). Going past it means drawing relative
	// to the camera's cell, in the shader as well as here.
	inline constexpr int kMaxSectionsXZ	= 1024;
	inline constexpr int kMaxSectionsY	= 256;

	/*!
		@brief
			Packs a section coordinate and LoD level into SectionRecord::posLod_ (10 / 8 / 10 / 3 bits).
		@param sx
			Section x at that level
		@param sy
			Section y at that level
		@param sz
			Section z at that level
		@param lod
			LoD level
		@return
			The packed word
		*/
	inline constexpr std::uint32_t packPosLod(int sx, int sy, int sz, int lod)
	{
		return static_cast<std::uint32_t>(sx & 1023)
			| static_cast<std::uint32_t>(sy & 255) << 10
			| static_cast<std::uint32_t>(sz & 1023) << 18
			| static_cast<std::uint32_t>(lod & 7) << 28;
	}
}
