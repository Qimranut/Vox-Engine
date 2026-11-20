/*************************************************************************************************************
\file       voxmap_codec.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares the per-section voxel codec: a palette plus a bit-packed or RLE index stream,
            shared by .voxmap files and the edit history.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Voxel
#include "../types/voxel_types.hpp"

// stds
#include <cstdint>
#include <functional>
#include <span>
#include <vector>


namespace voxel::voxmap
{
	/*!
		@brief
			Encodes one section's 32^3 material values into a compact payload.

			Writes a per-section palette plus either a bit-packed index stream
			(1/2/4/8 bits per voxel) or RLE pairs, whichever is smaller. Terrain
			sections hold two to four materials, so most cost 8 KB packed or far
			less with RLE.
		@param values
			kSectionVolume entries, in voxelIndex order
		@param out
			Appended: u16 P, u16 palette[P], pad to 4, then the stream
		@return
			The codec byte to store beside the payload
		*/
	std::uint8_t encodeSection(const std::uint8_t* values, std::vector<unsigned char>& out);

	/*!
		@brief
			Decodes a section payload written by encodeSection.
		@param payload
			The encoded bytes of one section
		@param codec
			The codec byte stored beside the payload
		@param remap
			File material id -> runtime id
		@param outValues
			kSectionVolume entries
		@return
			false on a malformed payload
		*/
	bool decodeSection(std::span<const unsigned char> payload, std::uint8_t codec,
					   const std::function<MaterialId(std::uint16_t)>& remap, std::uint8_t* outValues);
}
