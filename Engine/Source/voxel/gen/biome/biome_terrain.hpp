/*************************************************************************************************************
\file       biome_terrain.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      The uneroded landform of a version-3 recipe: compiled biomes blended by the biome map's weights,
            sampled on the world's voxel lattice, plus the coarse fields handed to erosion.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Voxel
#include "biome_definition.hpp"
#include "biome_map.hpp"
#include "height_ops.hpp"
#include "../heightfield.hpp"
#include "../recipe.hpp"

// stds
#include <cstdint>
#include <vector>


namespace util::thread { class JobQueue; }

namespace voxel
{
	/*!
		@brief
			How much a biome's landform is shrunk to suit a cell size.

			1 at its featureSize_ or above, in proportion below, never under a quarter.
		@param biome
			The biome whose featureSize_ is compared
		@param cellSize
			The biome map's cell size in metres
		@return
			The scale factor, 0.25..1
		*/
	double landformScale(const BiomeDefinition& biome, double cellSize);

	/** A BiomeDefinition's landform, ready to sample. */
	class CompiledBiome
	{
	public:

		/*!
			@brief
				Compiles a biome's layers, shrunk by scale, and computes its height bounds.

				Throws std::runtime_error when a layer does not compile.
			@param biome
				The biome definition to compile
			@param seed
				Seed the layers' noise derives from
			@param minWavelength
				Noise octaves finer than this (metres) are dropped
			@param scale
				landformScale() for the map's cell size
			*/
		CompiledBiome(const BiomeDefinition& biome, std::uint64_t seed, double minWavelength, double scale);

		/*!
			@brief
				Samples the summed, terraced relief at a world point.
			@param x
				World X position in metres
			@param z
				World Z position in metres
			@return
				Height about the biome's base in metres
			*/
		double relief(double x, double z) const;

		/*!
			@brief
				Gets the lower bound of base + relief about the datum, from the ops' ranges.
			@return
				The lower bound in metres
			*/
		double low() const { return low_; }

		/*!
			@brief
				Gets the upper bound of base + relief about the datum, from the ops' ranges.
			@return
				The upper bound in metres
			*/
		double high() const { return high_; }

		/*!
			@brief
				Gets the biome's scaled mean ground level about the datum.
			@return
				The base height in metres
			*/
		float base() const { return base_; }

		/*!
			@brief
				Gets the share of the relief kept right at a border with another biome.
			@return
				The edge relief, 0..1
			*/
		float edgeRelief() const { return edgeRelief_; }

	private:

		std::vector<CompiledLayer>	layers_;
		float						base_, edgeRelief_;
		double						terraceStep_, terraceSharpness_, terraceSlack_;
		double						low_	{ 0.0 }, high_	{ 0.0 };
	};

	/** What the biome landform hands to erosion, all on the same coarse grid. */
	struct BiomeFields
	{
		HeightField	height_;		// metres above the map's floor
		HeightField	erodibility_;	// 0..1 share of a droplet's digging
		HeightField	talus_;			// tangent of the steepest slope bedrock holds
	};

	/**
	 * The uneroded landform of a version-3 recipe: every biome's height
	 * blended by the biome map's weights. The biome counterpart of
	 * TerrainShape.
	 *
	 * Sampled on the world's voxel lattice: sample i of a row lies at world
	 * voxel originVoxel + i * step, an integer, and only then becomes
	 * metres. Two maps that overlap therefore evaluate the very same
	 * numbers where they do, whatever their origins.
	 *
	 */
	class BiomeTerrain
	{
	public:

		/*!
			@brief
				Builds the biome map and compiles every biome in the recipe.

				Throws std::runtime_error when the recipe has no biomes or one does not compile.
			@param recipe
				The version-3 recipe to take biomes, seed, origin and sizes from
			*/
		explicit BiomeTerrain(const Recipe& recipe);

		/*!
			@brief
				Samples height, erodibility and talus on the coarse erosion grid.
			@param width
				Coarse grid width; samples are erosion.cellVoxels voxels apart
			@param depth
				Coarse grid depth; samples are erosion.cellVoxels voxels apart
			@param jobs
				Worker queue to spread rows over, or null to run inline
			@return
				The fields handed to erosion
			*/
		BiomeFields build(int width, int depth, util::thread::JobQueue* jobs) const;

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
		double height(double x, double z) const;

		/*!
			@brief
				Picks the biome whose materials a point shows.

				Inside a blend band the choice between the two strongest biomes
				follows a noise, so the material border frays instead of tracing
				the cell edge.
			@param x
				World X position in metres
			@param z
				World Z position in metres
			@return
				Index of the chosen biome
			*/
		int coverBiome(double x, double z) const;

		/*!
			@brief
				World position along x of a voxel edge (offset 0) or centre (0.5).
			@param voxel
				Voxel offset from the map's origin
			@return
				The world X position in metres
			*/
		double worldX(double voxel) const { return (static_cast<double>(originVoxelX_) + voxel) * voxelSize_; }

		/*!
			@brief
				World position along z of a voxel edge (offset 0) or centre (0.5).
			@param voxel
				Voxel offset from the map's origin
			@return
				The world Z position in metres
			*/
		double worldZ(double voxel) const { return (static_cast<double>(originVoxelZ_) + voxel) * voxelSize_; }

		/*!
			@brief
				Gets the biome map the terrain is laid out on.
			@return
				The biome map
			*/
		const BiomeMap& map() const { return map_; }

	private:

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
		double heightFrom(const BiomeSample& sample, double x, double z) const;

		Recipe::Biomes				biomes_;
		std::vector<CompiledBiome>	compiled_;
		BiomeMap					map_;

		double						voxelSize_;
		std::int64_t				originVoxelX_, originVoxelZ_;
		int							cellVoxels_;
		double						floor_, ceiling_;		// heights are kept inside the map

		std::uint64_t				seedDither_;
		util::noise::FbmParams		ditherFbm_;
	};
}
