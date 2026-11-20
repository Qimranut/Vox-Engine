/*************************************************************************************************************
\file       simplex_noise.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Stateless, seeded simplex noise in 2D and 3D, and 2D noise with its analytic gradient.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// stds
#include <cstdint>


namespace util::noise
{
	/*!
		@brief
			Simplex gradient noise (Gustavson's formulation) with hashed lattice gradients.

			The lattice gradient is chosen by hashing (seed, corner) instead of a
			permutation table. Stateless and thread-safe, and any 64-bit seed is a
			distinct field. Frequencies are the caller's business: pass
			coordinates already multiplied by the frequency.
		@param seed
			Field seed
		@param x
			Sample x (already scaled)
		@param y
			Sample y (already scaled)
		@return
			Noise value in roughly [-1, 1]
		*/
	double noise2(std::uint64_t seed, double x, double y);

	/*!
		@brief
			noise2 together with its analytic gradient.

			For fractals that damp detail by the slope accumulated so far.
		@param seed
			Field seed
		@param x
			Sample x (already scaled)
		@param y
			Sample y (already scaled)
		@param dx
			Receives the partial derivative with respect to the (already scaled) x
		@param dy
			Receives the partial derivative with respect to the (already scaled) y
		@return
			The same value noise2 returns for these inputs
		*/
	double noise2Deriv(std::uint64_t seed, double x, double y, double& dx, double& dy);

	/*!
		@brief
			3D simplex noise with hashed lattice gradients.
		@param seed
			Field seed
		@param x
			Sample x (already scaled)
		@param y
			Sample y (already scaled)
		@param z
			Sample z (already scaled)
		@return
			Noise value in roughly [-1, 1]
		*/
	double noise3(std::uint64_t seed, double x, double y, double z);
}
