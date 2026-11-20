/*************************************************************************************************************
\file       voxmap_codec.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Implements the per-section voxel codec: builds a palette, picks the smaller of a bit-packed
            or RLE index stream, and decodes either back with full bounds checking.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "voxmap_codec.hpp"

// Voxel
#include "voxmap_format.hpp"

// stds
#include <array>
#include <cstring>


namespace voxel::voxmap
{
	namespace
	{
		/*!
			@brief
				Appends a 16-bit value in little-endian order.
			@param out
				Byte buffer to append to
			@param v
				Value to write
			*/
		void putU16(std::vector<unsigned char>& out, std::uint16_t v)
		{
			out.push_back(static_cast<unsigned char>(v & 0xFF));
			out.push_back(static_cast<unsigned char>(v >> 8));
		}

		/*!
			@brief
				Appends zero bytes until the buffer size is a multiple of 4.
			@param out
				Byte buffer to pad
			*/
		void pad4(std::vector<unsigned char>& out)
		{
			while (out.size() % 4 != 0)
				out.push_back(0);
		}

		/*!
			@brief
				Picks the bits per voxel (1, 2, 4 or 8) needed to index a palette.
			@param paletteCount
				Number of palette entries
			@return
				Bits per voxel index
			*/
		std::uint8_t bitsFor(std::size_t paletteCount)
		{
			if (paletteCount <= 2)  return 1;
			if (paletteCount <= 4)  return 2;
			if (paletteCount <= 16) return 4;
			return 8;
		}
	}

	/*!
		@brief
			Encodes one section as a palette plus the smaller of a bit-packed or RLE index stream.
		@param values
			kSectionVolume entries, in voxelIndex order
		@param out
			Appended: u16 P, u16 palette[P], pad to 4, then the stream
		@return
			The codec byte to store beside the payload
		*/
	std::uint8_t encodeSection(const std::uint8_t* values, std::vector<unsigned char>& out)
	{
		// Palette in first-appearance order; index lookup by value.
		std::array<std::int16_t, 256> indexOf;
		indexOf.fill(-1);
		std::vector<std::uint16_t> palette;
		palette.reserve(8);

		std::vector<std::uint8_t> indices(kSectionVolume);
		for (int i = 0; i < kSectionVolume; ++i)
		{
			const std::uint8_t v = values[i];
			if (indexOf[v] < 0)
			{
				indexOf[v] = static_cast<std::int16_t>(palette.size());
				palette.push_back(v);
			}
			indices[i] = static_cast<std::uint8_t>(indexOf[v]);
		}

		const std::uint8_t bits = bitsFor(palette.size());

		// Raw bit stream.
		std::vector<unsigned char> raw;
		raw.resize(static_cast<std::size_t>(kSectionVolume) * bits / 8);
		{
			std::uint64_t acc = 0;
			int accBits = 0;
			std::size_t pos = 0;
			for (int i = 0; i < kSectionVolume; ++i)
			{
				acc |= static_cast<std::uint64_t>(indices[i]) << accBits;
				accBits += bits;
				while (accBits >= 8)
				{
					raw[pos++] = static_cast<unsigned char>(acc & 0xFF);
					acc >>= 8;
					accBits -= 8;
				}
			}
		}

		// RLE pairs.
		std::vector<unsigned char> rle;
		{
			int i = 0;
			while (i < kSectionVolume)
			{
				const std::uint8_t idx = indices[i];
				int run = 1;
				while (run < 256 && i + run < kSectionVolume && indices[i + run] == idx)
					++run;
				rle.push_back(static_cast<unsigned char>(run - 1));
				rle.push_back(idx);
				i += run;
			}
		}

		const bool useRle = rle.size() < raw.size();

		putU16(out, static_cast<std::uint16_t>(palette.size()));
		for (const std::uint16_t p : palette)
			putU16(out, p);
		pad4(out);

		const std::vector<unsigned char>& stream = useRle ? rle : raw;
		out.insert(out.end(), stream.begin(), stream.end());

		return static_cast<std::uint8_t>(bits | (useRle ? kCodecRle : 0));
	}

	/*!
		@brief
			Decodes a section payload written by encodeSection, rejecting any malformed input.
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
					   const std::function<MaterialId(std::uint16_t)>& remap, std::uint8_t* outValues)
	{
		const std::uint8_t bits = codec & kCodecBitsMask;
		if (bits != 1 && bits != 2 && bits != 4 && bits != 8)
			return false;

		std::size_t pos = 0;
		const auto readU16 = [&](std::uint16_t& v) -> bool
		{
			if (pos + 2 > payload.size())
				return false;
			v = static_cast<std::uint16_t>(payload[pos] | (payload[pos + 1] << 8));
			pos += 2;
			return true;
		};

		std::uint16_t count = 0;
		if (!readU16(count) || count == 0 || count > 256)
			return false;

		std::array<MaterialId, 256> palette{};
		for (std::uint16_t i = 0; i < count; ++i)
		{
			std::uint16_t fileId = 0;
			if (!readU16(fileId))
				return false;
			palette[i] = remap(fileId);
		}
		while (pos % 4 != 0)
			++pos;

		if (codec & kCodecRle)
		{
			int i = 0;
			while (i < kSectionVolume)
			{
				if (pos + 2 > payload.size())
					return false;
				const int run = payload[pos] + 1;
				const std::uint8_t idx = payload[pos + 1];
				pos += 2;
				if (idx >= count || i + run > kSectionVolume)
					return false;
				std::memset(outValues + i, palette[idx], static_cast<std::size_t>(run));
				i += run;
			}
			return pos == payload.size();
		}

		const std::size_t needed = static_cast<std::size_t>(kSectionVolume) * bits / 8;
		if (payload.size() - pos != needed)
			return false;

		const std::uint32_t mask = (1u << bits) - 1u;
		std::uint64_t acc = 0;
		int accBits = 0;
		for (int i = 0; i < kSectionVolume; ++i)
		{
			if (accBits < bits)
			{
				acc |= static_cast<std::uint64_t>(payload[pos++]) << accBits;
				accBits += 8;
			}
			const std::uint32_t idx = static_cast<std::uint32_t>(acc) & mask;
			acc >>= bits;
			accBits -= bits;
			if (idx >= count)
				return false;
			outValues[i] = palette[idx];
		}
		return true;
	}
}
