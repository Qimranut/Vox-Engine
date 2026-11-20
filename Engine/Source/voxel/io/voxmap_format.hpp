/*************************************************************************************************************
\file       voxmap_format.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Constants and the directory entry layout of the .voxmap file format (level-0 voxel world,
            little-endian): magic, version, section kinds and the codec byte bits.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// stds
#include <cstdint>


namespace voxel::voxmap
{
	// ---------------------------------------------------------------------
	//  .voxmap - a cooked voxel world, level 0 only. Little-endian.
	//
	//  Header (fixed part)
	//    u32  magic 'VOXM'          u32  version          u32  headerBytes
	//    f32  voxelSize             f32  origin x,y,z
	//    u32  voxels x,y,z          u32  sections x,y,z
	//    u32  compression (0)       u32  flags
	//    u64  seed                  u64  materialTableHash
	//    u32  recipePathBytes, then the UTF-8 path, padded to 4
	//  Materials
	//    u32  count, then count x { u32 nameBytes; name; pad4 }
	//    File material id = index in this list.
	//  Directory
	//    u32  count (= sections x*y*z, sectionIndex order)
	//    count x DirEntry (16 bytes)
	//  Payloads, each 4-byte aligned
	//    u16 paletteCount P; u16 palette[P]; pad4; then bits or RLE pairs
	//  Trailer
	//    u32  'VOXE'
	//
	//  Only level 0 is stored: coarser levels and meshes are pure
	//  functions of it and the material table, rebuild in a few hundred
	//  milliseconds off-thread, and would go stale the moment a designer
	//  changed a LoD priority.
	// ---------------------------------------------------------------------

	inline constexpr std::uint32_t kMagic		= 0x4D584F56u;	// "VOXM"
	inline constexpr std::uint32_t kTrailer		= 0x45584F56u;	// "VOXE"
	inline constexpr std::uint32_t kVersion		= 1;

	inline constexpr std::uint32_t kCompressionNone	= 0;

	inline constexpr std::uint8_t kKindUniform	= 0;
	inline constexpr std::uint8_t kKindPalette	= 1;

	// codec byte: bits 0-3 bits per voxel (1, 2, 4, 8), bit 4 = RLE pairs
	inline constexpr std::uint8_t kCodecBitsMask	= 0x0F;
	inline constexpr std::uint8_t kCodecRle			= 0x10;

	struct DirEntry
	{
		std::uint64_t	offset_			{ 0 };	// from file start; 0 for uniform
		std::uint32_t	size_			{ 0 };	// payload bytes; 0 for uniform
		std::uint16_t	uniformMaterial_{ 0 };	// file id, when kind == kKindUniform
		std::uint8_t	kind_			{ kKindUniform };
		std::uint8_t	codec_			{ 0 };
	};

	static_assert(sizeof(DirEntry) == 16);
}
