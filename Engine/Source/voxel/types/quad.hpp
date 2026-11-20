/*************************************************************************************************************
\file       quad.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      voxel::Quad, the packed 64-bit merged face the mesher emits and the GPU and collider unpack,
            plus unswizzle() from a quad's (u, v, d) frame back to (x, y, z).

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Voxel
#include "voxel_types.hpp"

// stds
#include <cstdint>


namespace voxel
{
	/**
	 * One merged face in 8 bytes - the geometry format the GPU reads
	 * directly. The shader and the collider unpack the same bit layout:
	 *
	 *   bits  0- 2  face  = axis << 1 | side
	 *   bits  3- 6  length - 1   extent along u
	 *   bits  7-10  width  - 1   extent along v
	 *   bits 11-15  Z
	 *   bits 16-20  Y            owning voxel inside the section
	 *   bits 21-25  X
	 *   bits 26-41  material id
	 *   bits 42-49  corner AO, 4 corners x 2 bits, corner k = (uBit << 1) | vBit
	 *   bits 50-63  free
	 *
	 * (u, v, d) per axis:  Y: X, Z, Y    Z: X, Y, Z    X: Y, Z, X
	 *
	 * The stored position is the voxel that owns the face. For side 1 the
	 * face plane sits at d + 1; the vertex shader adds that.
	 *
	 */
	struct Quad
	{
		std::uint64_t bits_{ 0 };

		static constexpr int kMaxSize = 16;

		/*!
			@brief
				Packs one merged face into the bit layout above.
			@param face
				axis << 1 | side
			@param length
				Extent along u, 1..16
			@param width
				Extent along v, 1..16
			@param x
				Owning voxel x inside the section
			@param y
				Owning voxel y inside the section
			@param z
				Owning voxel z inside the section
			@param material
				Material id
			@param ao
				Corner AO, 4 corners x 2 bits
			@return
				The packed quad
			*/
		static constexpr Quad pack(std::uint8_t face, int length, int width,
								   int x, int y, int z, std::uint16_t material, std::uint8_t ao)
		{
			std::uint64_t b = 0;
			b |= static_cast<std::uint64_t>(face & 7u);
			b |= static_cast<std::uint64_t>((length - 1) & 15) << 3;
			b |= static_cast<std::uint64_t>((width - 1) & 15) << 7;
			b |= static_cast<std::uint64_t>(z & 31) << 11;
			b |= static_cast<std::uint64_t>(y & 31) << 16;
			b |= static_cast<std::uint64_t>(x & 31) << 21;
			b |= static_cast<std::uint64_t>(material) << 26;
			b |= static_cast<std::uint64_t>(ao) << 42;
			return Quad{ b };
		}

		/*!
			@brief
				The face code.
			@return
				axis << 1 | side
			*/
		constexpr std::uint8_t	face() const		{ return static_cast<std::uint8_t>(bits_ & 7u); }

		/*!
			@brief
				The face axis (0 = Y, 1 = Z, 2 = X).
			@return
				The axis
			*/
		constexpr int			axis() const		{ return face() >> 1; }

		/*!
			@brief
				The face side (0 faces -, 1 faces +).
			@return
				The side
			*/
		constexpr int			side() const		{ return face() & 1; }

		/*!
			@brief
				Extent along u.
			@return
				1..16
			*/
		constexpr int			length() const		{ return static_cast<int>((bits_ >> 3) & 15u) + 1; }

		/*!
			@brief
				Extent along v.
			@return
				1..16
			*/
		constexpr int			width() const		{ return static_cast<int>((bits_ >> 7) & 15u) + 1; }

		/*!
			@brief
				Owning voxel z inside the section.
			@return
				0..31
			*/
		constexpr int			z() const			{ return static_cast<int>((bits_ >> 11) & 31u); }

		/*!
			@brief
				Owning voxel y inside the section.
			@return
				0..31
			*/
		constexpr int			y() const			{ return static_cast<int>((bits_ >> 16) & 31u); }

		/*!
			@brief
				Owning voxel x inside the section.
			@return
				0..31
			*/
		constexpr int			x() const			{ return static_cast<int>((bits_ >> 21) & 31u); }

		/*!
			@brief
				The material id.
			@return
				The material id
			*/
		constexpr std::uint16_t	material() const	{ return static_cast<std::uint16_t>((bits_ >> 26) & 0xFFFFu); }

		/*!
			@brief
				The packed corner AO.
			@return
				4 corners x 2 bits
			*/
		constexpr std::uint8_t	ao() const			{ return static_cast<std::uint8_t>((bits_ >> 42) & 0xFFu); }

		/*!
			@brief
				Low 32 bits; what the SSBO stores is uvec2(lo, hi).
			@return
				The low word
			*/
		constexpr std::uint32_t lo() const { return static_cast<std::uint32_t>(bits_ & 0xFFFFFFFFu); }

		/*!
			@brief
				High 32 bits; what the SSBO stores is uvec2(lo, hi).
			@return
				The high word
			*/
		constexpr std::uint32_t hi() const { return static_cast<std::uint32_t>(bits_ >> 32); }

		/*!
			@brief
				Position along u in the (u, v, d) frame of this quad's axis.
			@return
				The u coordinate
			*/
		constexpr int u() const { return axis() == 2 ? y() : x(); }

		/*!
			@brief
				Position along v in the (u, v, d) frame of this quad's axis.
			@return
				The v coordinate
			*/
		constexpr int v() const { return axis() == 0 ? z() : (axis() == 1 ? y() : z()); }

		/*!
			@brief
				Position along d in the (u, v, d) frame of this quad's axis.
			@return
				The d coordinate
			*/
		constexpr int d() const { return axis() == 0 ? y() : (axis() == 1 ? z() : x()); }

		/*!
			@brief
				Bitwise equality.
			@param o
				The other quad
			@return
				true when the bits match
			*/
		constexpr bool operator==(const Quad& o) const { return bits_ == o.bits_; }
	};

	/*!
		@brief
			(u, v, d) -> (x, y, z) for a given axis. Mirrors the shader's swizzle.
		@param axis
			Face axis (0 = Y, 1 = Z, 2 = X)
		@param u
			u coordinate
		@param v
			v coordinate
		@param d
			d coordinate
		@return
			The (x, y, z) coordinate
		*/
	inline constexpr glm::ivec3 unswizzle(int axis, int u, int v, int d)
	{
		switch (axis)
		{
		case 0:  return { u, d, v };
		case 1:  return { u, v, d };
		default: return { d, u, v };
		}
	}
}
