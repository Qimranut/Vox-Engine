/*************************************************************************************************************
\file       terrain_generator.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares TerrainGenerator, which turns a Recipe into eroded heightmap terrain in two stages
            (whole-map prepass, then per-column voxel fill), and TileOptions for one streamed tile.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Voxel
#include "heightfield.hpp"
#include "recipe.hpp"
#include "stage_observer.hpp"
#include "surface_rules.hpp"
#include "../types/material_table.hpp"
#include "../world/world.hpp"

// stds
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>


namespace util::thread { class JobQueue; }

namespace voxel
{
	class BiomeTerrain;

	/**
	 * Makes a generator produce one TILE of a streamed terrain instead of a
	 * whole map. Its recipe then describes a window: the tile's core plus pad_
	 * voxels all round. Inside the core the terrain is eroded as usual; towards
	 * the core's border it is faded back to the biomes' uneroded ground, which
	 * depends on nothing but the position in the world. For pure_ voxels inside
	 * the border, and everywhere in the pad, it IS that ground, so the tile
	 * next door - which does the same from its side - agrees with this one
	 * wherever either looks across the border: the seam is the same terrain
	 * computed twice. Lakes that reach into the faded band are dropped.
	 *
	 * pure_ has to cover how far the later passes look sideways (the surface
	 * rules' slope and majority filters, the upsample, the spike clamp);
	 * TileGenerator derives that reach from the recipe and checks it.
	 *
	 * Biome terrain only. See gen/stream/tile_generator.hpp, which fills this in.
	 *
	 */
	struct TileOptions
	{
		std::uint64_t	erosionSalt_	{ 0 };		// mixed into the droplet seed: a different rain on every tile
		glm::ivec2		voxelOffset_	{ 0 };		// of the window in the world, for noise sampled by voxel (caves)
		int				core_			{ 0 };		// voxels along one side of the tile proper
		int				pad_			{ 0 };
		int				pure_			{ 0 };
		int				blend_			{ 0 };		// width of the fade from uneroded to eroded, inside pure_
	};

	/**
	 * Eroded heightmap terrain from a Recipe, in two stages.
	 *
	 * The prepass works on the whole map at once: a smooth landform
	 * (TerrainShape, or BiomeTerrain when the recipe lists biomes), droplet then thermal erosion on a coarse grid, lakes
	 * by priority-flood, a smooth upsample to one sample per voxel column
	 * with slope-gated detail, and the surface rules. The fill then writes
	 * voxels from those maps, one job per section column.
	 *
	 * The output is a function of (recipe, seed) alone. Passes that run in
	 * parallel write only their own rows from frozen inputs; the passes
	 * that cannot (droplets, flooding) run on one thread in a fixed order.
	 * So the world is bit-identical whatever the thread count, which is
	 * what lets the recipe stand in for the cooked map.
	 *
	 * One source of a World among others (an authored-model importer is
	 * the other planned one); nothing downstream knows terrain exists.
	 *
	 */
	class TerrainGenerator
	{
	public:

		/*!
			@brief
				A generator for a whole map.
			@param recipe
				The terrain to make
			@param materials
				Material names in the recipe are resolved here; throws
				std::runtime_error when one is missing
			*/
		TerrainGenerator(const Recipe& recipe, const MaterialTable& materials);

		/*!
			@brief
				A generator for one tile of a streamed terrain.
			@param recipe
				The tile's window: size_ is core + 2 * pad a side, origin_ its corner,
				stream_.endless_ off. Must be biome terrain; throws otherwise
			@param materials
				Material names in the recipe are resolved here
			@param tile
				Core, pad, pure band, blend and erosion salt of the tile
			*/
		TerrainGenerator(const Recipe& recipe, const MaterialTable& materials, const TileOptions& tile);

		/*!
			@brief
				Dimensions a world for this recipe must have.
			@return
				The world dimensions
			*/
		WorldDims dims() const;

		/*!
			@brief
				buildMaps() then fill(): fills level 0.

				The caller rebuilds LoD levels afterwards.
			@param world
				Receives level 0
			@param jobs
				Queue for the parallel passes, or null
			@param progress
				Incremented workUnits() times over a full run, if given
			@param cancel
				Polled between units; a cancelled run leaves the world partial
			@param observer
				Shown the terrain after each prepass stage, if given
			@return
				false if cancelled
			*/
		bool generate(World& world, util::thread::JobQueue* jobs = nullptr,
					  std::atomic<int>* progress = nullptr, const std::atomic<bool>* cancel = nullptr,
					  GenStageObserver* observer = nullptr) const;

		/*!
			@brief
				Total progress increments of generate(), for progress bars.
			@return
				The number of increments
			*/
		int workUnits() const;

		/*!
			@brief
				The prepass on its own.
			@param jobs
				Queue for the parallel passes, or null
			@param progress
				Incremented as passes complete, if given
			@param cancel
				Polled between passes
			@param observer
				Shown the terrain after each stage (see stage_observer.hpp). It only
				looks: the maps are the same with or without one
			@return
				Empty if cancelled
			*/
		std::optional<TerrainMaps> buildMaps(util::thread::JobQueue* jobs = nullptr,
											 std::atomic<int>* progress = nullptr, const std::atomic<bool>* cancel = nullptr,
											 GenStageObserver* observer = nullptr) const;

		/*!
			@brief
				Writes level 0 from maps, parallel over section columns when a queue is given.
			@param world
				Receives level 0
			@param maps
				The prepass result
			@param jobs
				Queue to spread section columns over, or null
			@param progress
				Incremented as columns complete, if given
			@param cancel
				Polled between columns
			@return
				false if cancelled
			*/
		bool fill(World& world, const TerrainMaps& maps, util::thread::JobQueue* jobs = nullptr,
				  std::atomic<int>* progress = nullptr, const std::atomic<bool>* cancel = nullptr) const;

		/*!
			@brief
				The voxels of one section column, written into sections that belong to no World.

				What a streamed cell is built from, on a worker, before anything
				is installed.
			@param maps
				The prepass result
			@param x0
				The column's minimum corner in the maps' own columns, along x; need
				not be a multiple of 32 (a tile's core starts pad_ columns in)
			@param z0
				The column's minimum corner in the maps' own columns, along z
			@param column
				One section per 32 voxels of height, bottom first; every one is
				overwritten
			*/
		void fillColumnSections(const TerrainMaps& maps, int x0, int z0, std::span<Section> column) const;

	private:

		/*!
			@brief
				Writes one section column of level 0 into the world.
			@param world
				Receives the voxels
			@param maps
				The prepass result
			@param sx
				Section column index along x
			@param sz
				Section column index along z
			*/
		void fillColumn(World& world, const TerrainMaps& maps, int sx, int sz) const;

		/*!
			@brief
				Fills the voxels of one section column; at(sy) is the section to write for layer sy.
			@param maps
				The prepass result
			@param x0
				The column's minimum corner in the maps' columns, along x
			@param z0
				The column's minimum corner in the maps' columns, along z
			@param sectionsY
				Sections in the column
			@param at
				Returns the section to write for a layer
			*/
		template <typename At>
		void fillColumnInto(const TerrainMaps& maps, int x0, int z0, int sectionsY, At&& at) const;

		/*!
			@brief
				Fades a tile's eroded ground back to the uneroded base towards its border.
			@param coarse
				Eroded heights, blended in place
			@param sediment
				Net sediment, blended to zero alongside
			@param base
				The uneroded ground
			*/
		void blendTowardsBase(HeightField& coarse, HeightField& sediment, const HeightField& base) const;

		/*!
			@brief
				Undoes what the lake pass did in the faded band and drops the lakes that reach into it.
			@param coarse
				Heights after the lake pass, restored in the band
			@param lakes
				Water levels; lakes reaching the band are removed
			@param beforeLakes
				Heights before the lake pass
			*/
		void confineLakes(HeightField& coarse, HeightField& lakes, const HeightField& beforeLakes) const;

		/*!
			@brief
				0 in the pad and the pure band, 1 in the fully eroded middle; by coarse cell.
			@param cx
				Coarse cell along x
			@param cz
				Coarse cell along z
			@return
				The erosion weight
			*/
		float tileWeight(int cx, int cz) const;

		Recipe				recipe_;
		SurfaceMaterials	surface_;
		MaterialId			water_	{ kAir };

		std::uint64_t		seedErosion_, seedDetail_, seedCaves_, seedStrata_;

		// Biome terrain only (recipe_.biomes_.enabled_).
		std::shared_ptr<const BiomeTerrain>	biomes_;
		std::vector<BiomeCover>				covers_;		// one per recipe biome
		int									shellVoxels_	{ 0 };

		// Set when this generator makes one tile of a streamed terrain.
		std::optional<TileOptions>			tile_;
	};
}
