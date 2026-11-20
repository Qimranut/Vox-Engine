/*************************************************************************************************************
\file       surface_rules.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Implements the surface rules: Sobel slope on float heights, per-column cover classification
            with noise-jittered thresholds, a 5x5 majority filter, and the resulting TerrainMaps.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "surface_rules.hpp"

// Voxel
#include "hydrology.hpp"
#include "parallel_rows.hpp"

// Noise
#include "../../util/noise/fbm.hpp"
#include "../../util/noise/hash.hpp"

// GLM
#include <glm/glm.hpp>

// Profiling
#include "../../profile/profiler.hpp"

// stds
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>


namespace voxel
{
	namespace
	{
		enum Cover : std::uint8_t { Sand, Snow, Rock, Dirt, Grass, CoverCount };

		constexpr int kMajorityPasses = 2;

		/*!
			@brief
				Whether a water-level value holds water.
			@param level
				Water level, kNoWater where dry
			@return
				true when the cell is wet
			*/
		inline bool holdsWater(float level)
		{
			return level > kNoWater * 0.5f;
		}

		/** Slope thresholds as tangents, and the depths that go with a cover. */
		struct Ruleset
		{
			float	grassLow_, grassHigh_, rockLow_, rockHigh_, rockMid_, snowTan_;
			float	snowline_, sandDeposit_, soilDepth_, snowDepth_, sandDepth_;
		};

		/*!
			@brief
				Builds the jittered slope thresholds, as tangents, from angles in degrees.
			@param grassMaxDeg
				Steepest slope grass holds
			@param rockDeg
				Slope above which bare rock shows
			@param jitterDeg
				How far the thresholds wander either way
			@param snowMaxDeg
				Steepest slope snow holds
			@return
				The ruleset with its tangents filled; heights and depths are left for the caller
			*/
		Ruleset rulesetOf(float grassMaxDeg, float rockDeg, float jitterDeg, float snowMaxDeg)
		{
			// Jittered thresholds must stay tangents of real slopes.
			const auto tanDeg = [](float degrees) { return std::tan(glm::radians(std::clamp(degrees, 0.5f, 88.5f))); };

			Ruleset set{};
			set.grassLow_ = tanDeg(grassMaxDeg - jitterDeg);
			set.grassHigh_ = tanDeg(grassMaxDeg + jitterDeg);
			set.rockLow_ = tanDeg(rockDeg - jitterDeg);
			set.rockHigh_ = tanDeg(rockDeg + jitterDeg);
			set.rockMid_ = tanDeg(rockDeg);
			set.snowTan_ = tanDeg(snowMaxDeg);
			return set;
		}

		/*!
			@brief
				Index of the voxel whose centre lies just under a height in metres.
			@param metres
				Height in metres
			@param voxelSize
				Voxel edge in metres
			@return
				The voxel index
			*/
		inline int voxelBelow(float metres, float voxelSize)
		{
			return static_cast<int>(std::floor(metres / voxelSize - 0.5f));
		}
	}

	/*!
		@brief
			Slope of a height field as a tangent, from a Sobel stencil on the float heights.
		@param field
			Heights in metres
		@param x
			Column
		@param z
			Row
		@param reach
			Half-width of the stencil in cells, at least 1
		@return
			The slope's tangent
		*/
	float slopeTangent(const HeightField& field, int x, int z, int reach)
	{
		const int r = std::max(1, reach);
		const float gx = (field.clamped(x + r, z - r) + 2.f * field.clamped(x + r, z) + field.clamped(x + r, z + r))
			- (field.clamped(x - r, z - r) + 2.f * field.clamped(x - r, z) + field.clamped(x - r, z + r));
		const float gz = (field.clamped(x - r, z + r) + 2.f * field.clamped(x, z + r) + field.clamped(x + r, z + r))
			- (field.clamped(x - r, z - r) + 2.f * field.clamped(x, z - r) + field.clamped(x + r, z - r));

		const float scale = 1.f / (8.f * field.cell_ * static_cast<float>(r));
		return std::sqrt(gx * gx + gz * gz) * scale;
	}

	/*!
		@brief
			How many voxel columns away a column's cover can depend on the heights.
		@param recipe
			Supplies the slope reach and voxel size
		@return
			The slope reach plus two columns per majority-filter pass
		*/
	int classifyReachVoxels(const Recipe& recipe)
	{
		// The majority filter is 5 x 5: two columns a pass.
		const int slopeReach = std::max(1, static_cast<int>(std::lround(recipe.surface_.slopeReach_ / recipe.voxelSize_)));
		return slopeReach + 2 * kMajorityPasses;
	}

	/*!
		@brief
			Classifies every voxel column's cover and depths and builds the terrain maps.
		@param height
			Final surface in metres, one sample per voxel column; moved into the result
		@param sediment
			Same grid as height; net material gained or lost
		@param water
			Same grid as height; kNoWater where dry
		@param recipe
			Supplies the surface rules, voxel size, seed and origin
		@param materials
			Material ids resolved from the recipe's names
		@param jobs
			Rows are classified in parallel when given
		@param biomes
			Biome terrain inputs, or null; its per-column arrays are moved into the result
		@return
			The per-column terrain maps
		*/
	TerrainMaps classifySurface(HeightField height, const HeightField& sediment, const HeightField& water,
								const Recipe& recipe, const SurfaceMaterials& materials,
								util::thread::JobQueue* jobs, BiomeColumns* biomes)
	{
		VOX_PROFILE_FUNCTION_C(profile::colour::kVoxel);

		const Recipe::Surface& rule = recipe.surface_;
		const int width = height.width_, depth = height.depth_;
		const float voxelSize = recipe.voxelSize_;

		// One set of thresholds for the whole map, or one per biome.
		std::vector<Ruleset> sets;
		if (biomes == nullptr)
		{
			sets.push_back(rulesetOf(rule.grassMaxSlopeDeg_, rule.rockSlopeDeg_, rule.slopeJitterDeg_, rule.snowMaxSlopeDeg_));
			sets.back().snowline_ = rule.snowline_;
			sets.back().sandDeposit_ = rule.sandDeposit_;
			sets.back().soilDepth_ = rule.soilDepth_;
			sets.back().snowDepth_ = rule.snowDepth_;
			sets.back().sandDepth_ = rule.sandDepth_;
		}
		else
		{
			if (biomes->biome_.size() != height.size() || biomes->strataShift_.size() != height.size())
				throw std::runtime_error("classifySurface: biome columns do not match the height field");

			for (const BiomeCover& cover : biomes->covers_)
			{
				const BiomeDefinition::Rules& b = cover.rules_;
				sets.push_back(rulesetOf(b.grassMaxSlopeDeg_, b.rockSlopeDeg_, rule.slopeJitterDeg_, b.snowMaxSlopeDeg_));
				sets.back().snowline_ = biomes->datum_ + biomes->reliefScale_ * b.snowline_;
				sets.back().sandDeposit_ = b.sandDeposit_;
				sets.back().soilDepth_ = b.soilDepth_;
				sets.back().snowDepth_ = b.snowDepth_;
				sets.back().sandDepth_ = b.sandDepth_;
			}
		}

		const std::uint8_t* biomeOf = biomes ? biomes->biome_.data() : nullptr;

		// Biome terrain is laid out in world space, so its threshold noise is
		// too: the wander follows the ground when the map is moved.
		const std::int64_t originX = biomes ? std::llround(static_cast<double>(recipe.origin_.x) / voxelSize) : 0;
		const std::int64_t originZ = biomes ? std::llround(static_cast<double>(recipe.origin_.z) / voxelSize) : 0;

		util::noise::FbmParams jitter;
		jitter.frequency_ = 1.0 / rule.jitterWavelength_;
		jitter.octaves_ = 2;
		const std::uint64_t seedSlope = recipe.seed_ ^ util::noise::hashString("surface.slope");
		const std::uint64_t seedSnow = recipe.seed_ ^ util::noise::hashString("surface.snow");

		// Shore: spread each lake's level a little way onto dry land, one
		// cell per pass, so the bank can be tested against it.
		HeightField shore = water;
		{
			const int reach = std::max(0, static_cast<int>(std::ceil(rule.shoreReach_ / voxelSize)));
			HeightField next = shore;
			for (int pass = 0; pass < reach; ++pass)
			{
				forRows(jobs, depth, [&](int z)
				{
					for (int x = 0; x < width; ++x)
					{
						float level = shore.at(x, z);
						if (!holdsWater(level))
							level = std::max({ shore.clamped(x - 1, z), shore.clamped(x + 1, z),
											   shore.clamped(x, z - 1), shore.clamped(x, z + 1) });
						next.at(x, z) = level;
					}
				});
				std::swap(shore.h_, next.h_);
			}
		}

		// Slope is read over a few voxels: what decides the cover is the lie
		// of the hillside, not the ripple on it.
		const int slopeReach = std::max(1, static_cast<int>(std::lround(rule.slopeReach_ / voxelSize)));

		std::vector<std::uint8_t> cover(height.size()), slopeShare(height.size());

		forRows(jobs, depth, [&](int z)
		{
			for (int x = 0; x < width; ++x)
			{
				const std::size_t i = height.index(x, z);
				const float h = height.h_[i];
				const float tangent = slopeTangent(height, x, z, slopeReach);

				const Ruleset& set = sets[biomeOf ? biomeOf[i] : 0];
				const double px = static_cast<double>(originX + x) * voxelSize, pz = static_cast<double>(originZ + z) * voxelSize;
				const float wander = static_cast<float>(std::clamp(0.5 + 0.5 * util::noise::fbm2(seedSlope, px, pz, jitter), 0.0, 1.0));
				const float grassMax = set.grassLow_ + (set.grassHigh_ - set.grassLow_) * wander;
				const float rockMin = set.rockLow_ + (set.rockHigh_ - set.rockLow_) * wander;

				Cover c;
				if (holdsWater(water.h_[i]) && h < water.h_[i])
					c = Sand;
				else if (holdsWater(shore.h_[i]) && h - shore.h_[i] < rule.shoreBand_)
					c = Sand;
				else if (sediment.h_[i] > set.sandDeposit_ && h < rule.sandMaxHeight_ && tangent < grassMax)
					c = Sand;
				else if (tangent < set.snowTan_
					&& h > set.snowline_ + rule.snowJitter_ * static_cast<float>(util::noise::fbm2(seedSnow, px, pz, jitter)))
					c = Snow;
				else if (tangent > rockMin)
					c = Rock;
				else if (tangent > grassMax)
					c = Dirt;
				else
					c = Grass;

				cover[i] = c;
				slopeShare[i] = static_cast<std::uint8_t>(std::clamp(tangent / set.rockMid_, 0.f, 1.f) * 255.f);
			}
		});

		// Majority filter: a cell whose cover holds under a third of its
		// 5x5 block takes the block's commonest. Ties go to the lowest
		// Cover value, so the outcome is fixed. Each pass reads one buffer
		// and writes the other.
		std::vector<std::uint8_t> settled(cover.size());
		for (int pass = 0; pass < kMajorityPasses; ++pass)
		{
			forRows(jobs, depth, [&](int z)
			{
				for (int x = 0; x < width; ++x)
				{
					int count[CoverCount] = {};
					for (int dz = -2; dz <= 2; ++dz)
					for (int dx = -2; dx <= 2; ++dx)
						++count[cover[height.index(std::clamp(x + dx, 0, width - 1), std::clamp(z + dz, 0, depth - 1))]];

					const std::size_t i = height.index(x, z);
					std::uint8_t best = cover[i];
					if (count[best] < 9)
					{
						for (std::uint8_t c = 0; c < CoverCount; ++c)
							if (count[c] > count[best])
								best = c;
					}
					settled[i] = best;
				}
			});

			if (pass + 1 < kMajorityPasses)
				std::swap(cover, settled);
		}

		TerrainMaps maps;
		maps.width_ = width;
		maps.depth_ = depth;
		maps.surface_.resize(height.size());
		maps.waterTop_.resize(height.size());
		maps.topMaterial_.resize(height.size());
		maps.topDepth_.resize(height.size());
		maps.soilDepth_.resize(height.size());

		const int highest = recipe.size_.y - 2;
		const auto voxels = [voxelSize](float metres)
		{
			return static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(metres / voxelSize)), 0, 255));
		};

		for (std::size_t i = 0; i < height.size(); ++i)
		{
			const int surface = std::clamp(voxelBelow(height.h_[i], voxelSize), 1, highest);
			maps.surface_[i] = static_cast<std::int16_t>(surface);

			int waterTop = -1;
			if (holdsWater(water.h_[i]))
			{
				waterTop = std::min(voxelBelow(water.h_[i], voxelSize), highest);
				if (waterTop <= surface)
					waterTop = -1;
			}
			maps.waterTop_[i] = static_cast<std::int16_t>(waterTop);

			const Ruleset& set = sets[biomeOf ? biomeOf[i] : 0];
			const BiomeCover* biome = biomeOf ? &biomes->covers_[biomeOf[i]] : nullptr;
			const SurfaceMaterials& palette = biome ? biome->materials_ : materials;

			const float flatness = 1.f - static_cast<float>(slopeShare[i]) / 255.f;
			const std::uint8_t soil = voxels(set.soilDepth_ * flatness + std::max(0.f, sediment.h_[i]));

			switch (settled[i])
			{
			case Sand:
				maps.topMaterial_[i] = palette.sand_;
				maps.topDepth_[i] = std::max<std::uint8_t>(1, voxels(set.sandDepth_));
				maps.soilDepth_[i] = soil;
				break;
			case Snow:
				maps.topMaterial_[i] = palette.snow_;
				maps.topDepth_[i] = std::max<std::uint8_t>(1, voxels(set.snowDepth_));
				maps.soilDepth_[i] = soil;
				break;
			case Rock:
				maps.topMaterial_[i] = biome ? biome->rockAt(surface + biomes->strataShift_[i]) : materials.base_;
				maps.topDepth_[i] = 1;
				maps.soilDepth_[i] = 0;
				break;
			case Dirt:
				maps.topMaterial_[i] = palette.under_;
				maps.topDepth_[i] = 1;
				maps.soilDepth_[i] = soil;
				break;
			default:
				maps.topMaterial_[i] = palette.top_;
				maps.topDepth_[i] = 1;
				maps.soilDepth_[i] = soil;
				break;
			}
		}

		if (biomes)
		{
			maps.biome_ = std::move(biomes->biome_);
			maps.strataShift_ = std::move(biomes->strataShift_);
		}

		maps.height_ = std::move(height);
		return maps;
	}
}
