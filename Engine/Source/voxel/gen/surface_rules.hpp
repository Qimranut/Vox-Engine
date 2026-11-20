/*************************************************************************************************************
\file       surface_rules.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares the surface rules: slope measurement and classifySurface, which decides each voxel
            column's cover (sand, snow, rock, dirt, grass) and depths, per recipe or per biome.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Voxel
#include "heightfield.hpp"
#include "recipe.hpp"

// stds
#include <cstdint>
#include <span>
#include <vector>


namespace util::thread { class JobQueue; }

namespace voxel
{
	/** Material ids the surface rules choose between, resolved from the recipe's names. */
	struct SurfaceMaterials
	{
		MaterialId	top_	{ kAir };
		MaterialId	under_	{ kAir };
		MaterialId	base_	{ kAir };
		MaterialId	sand_	{ kAir };
		MaterialId	snow_	{ kAir };
	};

	/**
	 * One biome's share of the surface rules, with its material names
	 * resolved: what classifySurface and the column fill look up per column
	 * in biome terrain.
	 *
	 */
	struct BiomeCover
	{
		SurfaceMaterials		materials_;		// base_ is the biome's own rock
		BiomeDefinition::Rules	rules_;

		/** Rock bands bottom to top, repeating; empty means materials_.base_ throughout. */
		struct Band
		{
			MaterialId	material_	{ kAir };
			int			voxels_		{ 1 };
		};
		std::vector<Band>		strata_;
		int						strataVoxels_	{ 0 };	// one full repeat

		// False when the biome's rock is just the recipe's deep base: then
		// there is no shell to write and buried sections stay uniform.
		bool					ownRock_		{ false };

		/*!
			@brief
				The biome's rock at a height.
			@param y
				Voxel height, already shifted by the column's strataShift_
			@return
				The strata band's material at y, or materials_.base_ without strata
			*/
		MaterialId rockAt(int y) const
		{
			if (strata_.empty())
				return materials_.base_;

			int within = y % strataVoxels_;
			if (within < 0)
				within += strataVoxels_;
			for (const Band& band : strata_)
			{
				if (within < band.voxels_)
					return band.material_;
				within -= band.voxels_;
			}
			return strata_.back().material_;
		}
	};

	/** Biome terrain's extra inputs to classifySurface. */
	struct BiomeColumns
	{
		std::span<const BiomeCover>	covers_;		// one per recipe biome
		std::vector<std::uint8_t>	biome_;			// per column, index into covers_
		std::vector<std::int8_t>	strataShift_;	// per column, voxels
		float						datum_			{ 0.f };	// biome snowlines are about this height
		float						reliefScale_	{ 1.f };
	};

	/*!
		@brief
			Slope of a height field as a tangent (rise over run, both in metres).

			From a Sobel stencil on the float heights. Measuring it on rounded
			voxel heights instead makes every one-voxel step read as 45 degrees.
		@param field
			Heights in metres
		@param x
			Column
		@param z
			Row
		@param reach
			Half-width of the stencil in cells; wider reads the hillside rather
			than the ripple on it
		@return
			The slope's tangent
		*/
	float slopeTangent(const HeightField& field, int x, int z, int reach = 1);

	/*!
		@brief
			Decides what each voxel column shows and what lies under it.

			From slope, altitude, deposited sediment and water:

			  under or beside water, or low ground buried in sediment  -> sand
			  above the snowline and not too steep to hold it         -> snow
			  steeper than rock                                        -> base, no soil
			  steeper than grass                                       -> under
			  otherwise                                                -> top

			Thresholds wander with a coarse noise so the boundaries are not
			contour lines, and a majority filter removes what speckle is left.
		@param height
			Final surface in metres, one sample per voxel column
		@param sediment
			Same grid as height; net material gained or lost
		@param water
			Same grid as height; kNoWater where dry
		@param recipe
			Supplies the surface rules, voxel size, seed and origin
		@param materials
			Material ids resolved from the recipe's names
		@param jobs
			Rows are classified in parallel when given; every pass reads one
			buffer and writes another, so the result is the same either way
		@param biomes
			Biome terrain: each column then takes its thresholds, depths and
			materials from its biome's cover instead of from recipe.surface_
			(which still supplies the jitter, the shore and the deep base). Moved
			from: its per-column arrays end up in the returned maps
		@return
			The per-column terrain maps
		*/
	TerrainMaps classifySurface(HeightField height, const HeightField& sediment, const HeightField& water,
								const Recipe& recipe, const SurfaceMaterials& materials,
								util::thread::JobQueue* jobs = nullptr, BiomeColumns* biomes = nullptr);

	/*!
		@brief
			How many voxel columns away a column's cover can depend on the heights, away from any lake.

			The slope's reach plus the majority filter's passes. A tile of a
			streamed terrain keeps at least this much identical ground on both
			sides of a seam (gen/stream/tile_generator.hpp).
		@param recipe
			Supplies the slope reach and voxel size
		@return
			The reach in voxel columns
		*/
	int classifyReachVoxels(const Recipe& recipe);
}
