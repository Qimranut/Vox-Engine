/*************************************************************************************************************
\file       hash.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Deterministic hashing for procedural generation: splitmix64, a stateless 64-bit mix,
            integer lattice hashing and FNV-1a string hashing for per-field seeds.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// stds
#include <cstdint>
#include <string_view>


namespace util::noise
{
	/*!
		@brief
			splitmix64 step: advances the state and returns the next 64-bit word.

			The standard way to turn one seed into many, and to seed a PCG stream.
		@param state
			Advanced in place
		@return
			The next 64-bit word
		*/
	inline std::uint64_t splitmix64(std::uint64_t& state)
	{
		std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
		z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
		z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
		return z ^ (z >> 31);
	}

	/*!
		@brief
			Stateless 64-bit mix (splitmix64 finaliser).

			Bijective, so distinct inputs never collide.
		@param x
			Value to mix
		@return
			The mixed value
		*/
	inline std::uint64_t hash64(std::uint64_t x)
	{
		x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
		x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
		return x ^ (x >> 31);
	}

	/*!
		@brief
			Hash of an integer lattice point under a seed.

			Pure: the same (seed, x, y, z) gives the same value on every thread
			and every run, which is what makes terrain generation deterministic.
		@param seed
			Field seed
		@param x
			Lattice x
		@param y
			Lattice y
		@param z
			Lattice z (0 for 2D)
		@return
			The 64-bit hash
		*/
	inline std::uint64_t hashCoords(std::uint64_t seed, std::int64_t x, std::int64_t y, std::int64_t z = 0)
	{
		std::uint64_t h = seed;
		h ^= hash64(static_cast<std::uint64_t>(x) * 0x9E3779B97F4A7C15ull);
		h ^= hash64(static_cast<std::uint64_t>(y) * 0xC2B2AE3D27D4EB4Full);
		h ^= hash64(static_cast<std::uint64_t>(z) * 0x165667B19E3779F9ull);
		return hash64(h);
	}

	/*!
		@brief
			FNV-1a over a string, for deriving one seed per named noise field from the recipe seed.
		@param s
			String to hash
		@return
			The 64-bit FNV-1a hash
		*/
	inline std::uint64_t hashString(std::string_view s)
	{
		std::uint64_t h = 0xCBF29CE484222325ull;
		for (const char c : s)
		{
			h ^= static_cast<unsigned char>(c);
			h *= 0x100000001B3ull;
		}
		return h;
	}
}
