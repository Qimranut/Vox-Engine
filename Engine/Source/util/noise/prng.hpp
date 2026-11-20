/*************************************************************************************************************
\file       prng.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Pcg32, a small seeded PCG32 (XSH RR) random generator for deterministic procedural placement.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Noise
#include "hash.hpp"

// stds
#include <cstdint>


namespace util::noise
{
	/**
	 * PCG32 (XSH RR): small, fast, and good enough for procedural placement.
	 * Two generators built from the same seed produce the same sequence.
	 *
	 */
	class Pcg32
	{
	public:

		/*!
			@brief
				Seeds the generator through splitmix64 and warms it up.
			@param seed
				Any 64-bit value; expanded through splitmix64
			@param stream
				Selects one of 2^63 independent sequences for the same seed
			*/
		explicit Pcg32(std::uint64_t seed, std::uint64_t stream = 0)
			: inc_((stream << 1u) | 1u)
		{
			std::uint64_t s = seed;
			state_ = splitmix64(s);
			next();
			state_ += splitmix64(s);
			next();
		}

		/*!
			@brief
				Advances the state and returns the next 32 random bits.
			@return
				32 random bits
			*/
		std::uint32_t next()
		{
			const std::uint64_t old = state_;
			state_ = old * 6364136223846793005ull + inc_;
			const std::uint32_t xorshifted = static_cast<std::uint32_t>(((old >> 18u) ^ old) >> 27u);
			const std::uint32_t rot = static_cast<std::uint32_t>(old >> 59u);
			return (xorshifted >> rot) | (xorshifted << ((0u - rot) & 31u));
		}

		/*!
			@brief
				Uniform float in [0, 1) from the top 24 bits of next().
			@return
				The random float
			*/
		float nextFloat()
		{
			return static_cast<float>(next() >> 8) * (1.0f / 16777216.0f);
		}

		/*!
			@brief
				Uniform integer in [0, bound). bound must be > 0.
			@param bound
				Exclusive upper bound
			@return
				The random integer
			*/
		std::uint32_t nextBelow(std::uint32_t bound)
		{
			return static_cast<std::uint32_t>((static_cast<std::uint64_t>(next()) * bound) >> 32);
		}

	private:

		std::uint64_t state_	{ 0 };
		std::uint64_t inc_		{ 1 };
	};
}
