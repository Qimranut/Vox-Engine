/*************************************************************************************************************
\file       terrain_shape.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Implements TerrainShape: the version-2 landform as a pure function of (seed, position).

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "terrain_shape.hpp"

// Noise
#include "../../util/noise/hash.hpp"
#include "../../util/noise/simplex_noise.hpp"

// Voxel
#include "parallel_rows.hpp"

// Profiling
#include "../../profile/profiler.hpp"

// stds
#include <algorithm>
#include <cmath>


namespace voxel
{
	namespace
	{
		// Octaves with a wavelength under this many grid cells are dropped.
		constexpr double kMinWavelengthCells = 4.0;
	}

	/*!
		@brief
			Prepares the noise parameters and seeds for a recipe's shape.
		@param recipe
			Supplies the shape, the map size and the seed
		@param cell
			Spacing of the sampling grid in metres; octaves under 4 cells are dropped
		*/
	TerrainShape::TerrainShape(const Recipe& recipe, float cell)
		: shape_(recipe.shape_)
		, cell_(cell)
		, sizeX_(static_cast<double>(recipe.size_.x) * recipe.voxelSize_)
		, sizeZ_(static_cast<double>(recipe.size_.z) * recipe.voxelSize_)
	{
		const double minWavelength = kMinWavelengthCells * cell_;
		warpFbm_ = shape_.warp_.fbm(minWavelength);
		massifFbm_ = shape_.massif_.fbm(minWavelength);
		detailFbm_ = shape_.detail_.fbm(minWavelength);

		// The ring's outline wanders on the scale of the map itself.
		wobbleFbm_.frequency_ = 1.0 / (0.6 * std::max(sizeX_, sizeZ_));
		wobbleFbm_.octaves_ = 2;

		using util::noise::hashString;
		seedWarp_ = recipe.seed_ ^ hashString("shape.warp");
		seedMassif_ = recipe.seed_ ^ hashString("shape.massif");
		seedDetail_ = recipe.seed_ ^ hashString("shape.detail");
		seedWobble_ = recipe.seed_ ^ hashString("shape.ring");
		seedBasin_ = recipe.seed_ ^ hashString("shape.basins");
	}

	/*!
		@brief
			The landform's height at a position: base, ring-masked massif, detail, then basins.
		@param x
			Position in metres from the map's minimum corner, along x
		@param z
			Position in metres from the map's minimum corner, along z
		@return
			Height in metres
		*/
	double TerrainShape::height(double x, double z) const
	{
		using namespace util::noise;

		double wx = x, wz = z;
		if (shape_.warp_.amplitude_ != 0.0)
			domainWarp2(seedWarp_, x, z, shape_.warp_.amplitude_, warpFbm_, wx, wz);

		double mask = 1.0;
		if (shape_.ring_.enabled_)
		{
			// Rounded-square distance: 0 at the centre, 1 on the map edge.
			const double cx = 2.0 * x / sizeX_ - 1.0;
			const double cz = 2.0 * z / sizeZ_ - 1.0;
			const double cx2 = cx * cx, cz2 = cz * cz;
			double r = std::sqrt(std::sqrt(cx2 * cx2 + cz2 * cz2));
			r += shape_.ring_.wobble_ * fbm2(seedWobble_, x, z, wobbleFbm_);

			mask = smoothstep(shape_.ring_.inner_, shape_.ring_.outer_, r);
			mask = std::pow(mask, static_cast<double>(shape_.ring_.power_));
		}

		const double massif = ridgedSmooth2(seedMassif_, wx, wz, massifFbm_);
		const double hills = dampedFbm2(seedDetail_, wx, wz, detailFbm_, shape_.detailDamping_);

		const double floor = shape_.massifFloor_;
		const double interior = shape_.detailInterior_;

		double h = shape_.baseHeight_
			+ mask * shape_.massif_.amplitude_ * (floor + (1.0 - floor) * massif)
			+ shape_.detail_.amplitude_ * hills * (interior + (1.0 - interior) * mask);

		for (std::size_t b = 0; b < shape_.basins_.size(); ++b)
		{
			const Recipe::Shape::Basin& basin = shape_.basins_[b];
			const double bx = x - basin.centre_.x * sizeX_;
			const double bz = z - basin.centre_.y * sizeZ_;

			// A noisy radius so the shoreline is not a circle.
			const double lobe = basin.radius_ * 1.2;
			const double wobble = 1.0 + 0.18 * noise2(seedBasin_ + b, x / lobe, z / lobe);
			const double d = std::sqrt(bx * bx + bz * bz) / (basin.radius_ * wobble);

			if (d < 1.0)
			{
				const double k = 1.0 - d * d;
				h -= basin.depth_ * k * k;
			}

			if (basin.rim_ > 0.f && d < 2.0)
			{
				const double q = (d - 1.0) / 0.25;
				h += basin.rim_ * std::exp(-q * q);
			}
		}

		return h;
	}

	/*!
		@brief
			Samples height() on a width x depth grid spaced cell_ apart.
		@param width
			Grid columns
		@param depth
			Grid rows
		@param jobs
			Queue to spread rows over, or null
		@return
			The sampled grid
		*/
	HeightField TerrainShape::build(int width, int depth, util::thread::JobQueue* jobs) const
	{
		VOX_PROFILE_FUNCTION_C(profile::colour::kVoxel);

		HeightField field(width, depth, cell_);

		forRows(jobs, depth, [this, &field, width](int z)
		{
			for (int x = 0; x < width; ++x)
				field.at(x, z) = static_cast<float>(height(static_cast<double>(x) * cell_, static_cast<double>(z) * cell_));
		});

		return field;
	}
}
