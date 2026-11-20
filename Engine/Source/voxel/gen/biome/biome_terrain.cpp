/*************************************************************************************************************
\file       biome_terrain.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Compiles biome landforms and blends them across the biome map into heights, cover biomes and
            the erodibility / talus fields erosion runs on.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "biome_terrain.hpp"

// Voxel
#include "../parallel_rows.hpp"

// Noise
#include "../../../util/noise/hash.hpp"

// GLM
#include <glm/glm.hpp>

// Profiling
#include "../../../profile/profiler.hpp"

// stds
#include <algorithm>
#include <cmath>
#include <stdexcept>


namespace voxel
{
	namespace
	{
		// Octaves with a wavelength under this many grid cells are dropped.
		constexpr double kMinWavelengthCells = 4.0;

		// A biome's relief reaches full strength this share of the cell size
		// in from a border with another biome.
		constexpr double kEdgeFade = 0.35;

		// How far the material border strays from the even-weight line, as a
		// weight difference, and the grain of that wander.
		constexpr double kDitherStrength = 0.18;
		constexpr double kDitherShare = 0.08;
		constexpr double kDitherMinWavelength = 3.0;
	}

	/*!
		@brief
			How much a biome's landform is shrunk to suit a cell size: 1 at or above featureSize_, never under 0.25.
		@param biome
			The biome whose featureSize_ is compared
		@param cellSize
			The biome map's cell size in metres
		@return
			The scale factor, 0.25..1
		*/
	double landformScale(const BiomeDefinition& biome, double cellSize)
	{
		if (!(biome.featureSize_ > 0.f))
			return 1.0;
		return std::clamp(cellSize / biome.featureSize_, 0.25, 1.0);
	}

	/*!
		@brief
			Compiles a biome's layers, shrunk by scale, and computes its height bounds.

			Throws std::runtime_error, naming the biome, when a layer does not compile.
		@param biome
			The biome definition to compile
		@param seed
			Seed the layers' noise derives from
		@param minWavelength
			Noise octaves finer than this (metres) are dropped
		@param scale
			landformScale() for the map's cell size
		*/
	CompiledBiome::CompiledBiome(const BiomeDefinition& biome, std::uint64_t seed, double minWavelength, double scale)
		: base_(static_cast<float>(biome.base_ * scale))
		, edgeRelief_(biome.edgeRelief_)
		, terraceStep_(biome.terrace_.step_ * std::max(scale, 0.5))	// steps under a metre or so stop reading as steps
		, terraceSharpness_(biome.terrace_.sharpness_)
		, terraceSlack_(biome.terrace_.slack_)
	{
		double low = 0.0, high = 0.0;
		for (std::size_t i = 0; i < biome.layers_.size(); ++i)
		{
			try
			{
				// Shrunk as a whole: every length by the same factor.
				HeightLayer layer = biome.layers_[i];
				layer.wavelength_ *= scale;
				layer.amplitude_ *= scale;
				layer.warp_.wavelength_ *= scale;
				layer.warp_.amplitude_ *= scale;
				layer.mask_.wavelength_ *= scale;

				layers_.emplace_back(layer, seed ^ util::noise::hash64(i + 1), minWavelength);
			}
			catch (const std::exception& e)
			{
				throw std::runtime_error("biome \"" + biome.name_ + "\": " + e.what());
			}
			low += layers_.back().low();
			high += layers_.back().high();
		}

		// A terrace moves a height by less than one step.
		low_ = base_ + low - terraceStep_;
		high_ = base_ + high + terraceStep_;
	}

	/*!
		@brief
			Sums the layers at a world point and folds in the terrace, keeping slack_ of the unfolded slope.
		@param x
			World X position in metres
		@param z
			World Z position in metres
		@return
			Height about the biome's base in metres
		*/
	double CompiledBiome::relief(double x, double z) const
	{
		double sum = 0.0;
		for (const CompiledLayer& layer : layers_)
			sum += layer.sample(x, z);

		if (!(terraceStep_ > 0.0))
			return sum;

		const double stepped = util::noise::terrace(sum, terraceStep_, terraceSharpness_);
		return stepped + terraceSlack_ * (sum - stepped);
	}

	/*!
		@brief
			Builds the biome map and compiles every biome in the recipe.

			Throws std::runtime_error when the recipe has no biomes or one does not compile.
		@param recipe
			The version-3 recipe to take biomes, seed, origin and sizes from
		*/
	BiomeTerrain::BiomeTerrain(const Recipe& recipe)
		: biomes_(recipe.biomes_)
		, map_(recipe.seed_, static_cast<int>(std::max<std::size_t>(1, recipe.biomes_.entries_.size())),
			   recipe.biomes_.size_, recipe.biomes_.blending_,
			   kMinWavelengthCells * recipe.erosion_.cellVoxels_ * recipe.voxelSize_)
		, voxelSize_(recipe.voxelSize_)
		, originVoxelX_(std::llround(static_cast<double>(recipe.origin_.x) / recipe.voxelSize_))
		, originVoxelZ_(std::llround(static_cast<double>(recipe.origin_.z) / recipe.voxelSize_))
		, cellVoxels_(recipe.erosion_.cellVoxels_)
		, floor_(2.0 * recipe.voxelSize_)
		, ceiling_((static_cast<double>(recipe.size_.y) - 3.0) * recipe.voxelSize_)
		, seedDither_(recipe.seed_ ^ util::noise::hashString("biomes.dither"))
	{
		if (!biomes_.enabled_ || biomes_.entries_.empty())
			throw std::runtime_error("BiomeTerrain: the recipe has no biomes");

		const double minWavelength = kMinWavelengthCells * cellVoxels_ * voxelSize_;
		for (const BiomeDefinition& biome : biomes_.entries_)
			compiled_.emplace_back(biome, recipe.seed_ ^ util::noise::hashString("biome." + biome.name_), minWavelength,
								   landformScale(biome, biomes_.size_));

		ditherFbm_.frequency_ = 1.0 / std::max(kDitherMinWavelength, kDitherShare * biomes_.size_);
		ditherFbm_.octaves_ = 2;
	}

	/*!
		@brief
			Blends every biome's height by a sample's weights, fading relief near borders.
		@param sample
			The biome map's sample at the point
		@param x
			World X position in metres
		@param z
			World Z position in metres
		@return
			Height in metres above the map's floor, clamped inside the map
		*/
	double BiomeTerrain::heightFrom(const BiomeSample& sample, double x, double z) const
	{
		const double fade = util::noise::smoothstep(0.0, kEdgeFade * biomes_.size_, sample.edge_);

		double h = 0.0;
		for (std::size_t b = 0; b < compiled_.size(); ++b)
		{
			const double w = sample.weight_[b];
			if (w <= 0.0)
				continue;

			const CompiledBiome& biome = compiled_[b];
			const double strength = biome.edgeRelief() + (1.0 - biome.edgeRelief()) * fade;
			h += w * (biome.base() + strength * biome.relief(x, z));
		}

		return std::clamp(biomes_.datumHeight_ + biomes_.reliefScale_ * h, floor_, ceiling_);
	}

	/*!
		@brief
			Samples the blended landform height at a world point.
		@param x
			World X position in metres
		@param z
			World Z position in metres
		@return
			Landform height in metres above the map's floor
		*/
	double BiomeTerrain::height(double x, double z) const
	{
		return heightFrom(map_.sample(x, z), x, z);
	}

	/*!
		@brief
			Picks the biome whose materials a point shows, dithering between the two strongest in a blend band.
		@param x
			World X position in metres
		@param z
			World Z position in metres
		@return
			Index of the chosen biome
		*/
	int BiomeTerrain::coverBiome(double x, double z) const
	{
		const BiomeSample sample = map_.sample(x, z);

		int first = sample.nearest_, second = -1;
		for (int b = 0; b < map_.count(); ++b)
		{
			if (b == first || sample.weight_[static_cast<std::size_t>(b)] <= 0.f)
				continue;
			if (second < 0 || sample.weight_[static_cast<std::size_t>(b)] > sample.weight_[static_cast<std::size_t>(second)])
				second = b;
		}
		if (second < 0)
			return first;

		// Decide between the pair in index order, so the answer does not
		// depend on which of the two happens to be nearer.
		const int lo = std::min(first, second), hi = std::max(first, second);
		const double lead = static_cast<double>(sample.weight_[static_cast<std::size_t>(lo)])
			- static_cast<double>(sample.weight_[static_cast<std::size_t>(hi)]);
		return lead + kDitherStrength * util::noise::fbm2(seedDither_, x, z, ditherFbm_) > 0.0 ? lo : hi;
	}

	/*!
		@brief
			Samples height, weighted erodibility and weighted talus tangent on the coarse erosion grid, by rows.
		@param width
			Coarse grid width; samples are erosion.cellVoxels voxels apart
		@param depth
			Coarse grid depth; samples are erosion.cellVoxels voxels apart
		@param jobs
			Worker queue to spread rows over, or null to run inline
		@return
			The fields handed to erosion
		*/
	BiomeFields BiomeTerrain::build(int width, int depth, util::thread::JobQueue* jobs) const
	{
		VOX_PROFILE_FUNCTION_C(profile::colour::kVoxel);

		const float cell = static_cast<float>(cellVoxels_ * voxelSize_);
		BiomeFields fields{ HeightField(width, depth, cell), HeightField(width, depth, cell), HeightField(width, depth, cell) };

		std::vector<float> talusTan(biomes_.entries_.size());
		for (std::size_t b = 0; b < talusTan.size(); ++b)
			talusTan[b] = std::tan(glm::radians(biomes_.entries_[b].talusDeg_));

		forRows(jobs, depth, [&](int z)
		{
			const double wz = worldZ(static_cast<double>(z) * cellVoxels_);
			for (int x = 0; x < width; ++x)
			{
				const double wx = worldX(static_cast<double>(x) * cellVoxels_);
				const BiomeSample sample = map_.sample(wx, wz);

				float erodibility = 0.f, talus = 0.f;
				for (std::size_t b = 0; b < talusTan.size(); ++b)
				{
					erodibility += sample.weight_[b] * biomes_.entries_[b].erodibility_;
					talus += sample.weight_[b] * talusTan[b];
				}

				fields.height_.at(x, z) = static_cast<float>(heightFrom(sample, wx, wz));
				fields.erodibility_.at(x, z) = erodibility;
				fields.talus_.at(x, z) = talus;
			}
		});

		return fields;
	}
}
