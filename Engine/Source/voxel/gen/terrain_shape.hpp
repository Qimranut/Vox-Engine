/*************************************************************************************************************
\file       terrain_shape.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares TerrainShape, the uneroded version-2 landform of a recipe (warped ridged massif,
            damped fbm detail, mountain ring, basins), sampled per position or over a grid.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Voxel
#include "heightfield.hpp"
#include "recipe.hpp"

// stds
#include <cstdint>


namespace util::thread { class JobQueue; }

namespace voxel
{
	/**
	 * The uneroded landform of a recipe: a warped ridged massif, slope-damped
	 * fbm detail, the mountain ring and the basins. Every sample is a pure
	 * function of (seed, position), so rows can be filled on any thread in
	 * any order.
	 *
	 */
	class TerrainShape
	{
	public:

		/*!
			@brief
				Prepares the noise parameters and seeds for a recipe's shape.
			@param recipe
				Supplies the shape, the map size and the seed
			@param cell
				Spacing of the grid this shape will be sampled on, in metres;
				octaves too fine for it are dropped
			*/
		TerrainShape(const Recipe& recipe, float cell);

		/*!
			@brief
				The landform's height at a position.
			@param x
				Position in metres from the map's minimum corner, along x
			@param z
				Position in metres from the map's minimum corner, along z
			@return
				Height in metres
			*/
		double height(double x, double z) const;

		/*!
			@brief
				Samples height() on a grid covering the map, one job per row when a queue is given.
			@param width
				Grid columns
			@param depth
				Grid rows
			@param jobs
				Queue to spread rows over, or null
			@return
				The sampled grid, spaced cell_ apart
			*/
		HeightField build(int width, int depth, util::thread::JobQueue* jobs) const;

	private:

		Recipe::Shape			shape_;
		float					cell_;
		double					sizeX_, sizeZ_;		// map extent in metres

		util::noise::FbmParams	warpFbm_, massifFbm_, detailFbm_, wobbleFbm_;
		std::uint64_t			seedWarp_, seedMassif_, seedDetail_, seedWobble_, seedBasin_;
	};
}
