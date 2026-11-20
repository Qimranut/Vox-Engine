/*************************************************************************************************************
\file       tile_generator.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares the generator side of streamed (endless) terrain: TileGenerator, which runs the
            prepass per tile and builds cells with their LoD levels from a tile's maps, and TileCache,
            the thread-safe cache of tiles near the player.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Voxel
#include "../heightfield.hpp"
#include "../recipe.hpp"
#include "../terrain_generator.hpp"
#include "../../edit/edit_history.hpp"
#include "../../types/material_table.hpp"
#include "../../world/world.hpp"

// GLM
#include <glm/glm.hpp>

// stds
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>


namespace voxel
{
	/**
	 * A streamed ("endless") terrain is never generated whole. It is made in
	 * TILES, square blocks of cells that each run the ordinary prepass - biome
	 * landform, droplets, scree, lakes, surface rules - on a window of their
	 * own, and from a tile's maps CELLS are filled: a cell is the World's page,
	 * 256 voxels a side, and the unit that streams in and out.
	 *
	 * What makes tiles meet is TileOptions (terrain_generator.hpp): erosion is
	 * faded out towards a tile's border, and the uneroded ground there is a
	 * function of the position in the world alone. A tile is therefore a pure
	 * function of (recipe, tile): it can be thrown away and made again, on any
	 * thread, in any order, and come out the same - which is what lets the
	 * streamer keep only the tiles near the player.
	 *
	 * Everything here is const and touches no World, so it runs on workers.
	 *
	 */

	/** One tile's maps, with the generator that made them (it also fills from them). */
	struct Tile
	{
		glm::ivec2							tile_	{ 0 };
		TerrainMaps							maps_;			// the window: core plus pad; height_ is dropped
		std::shared_ptr<const TerrainGenerator>	generator_;

		/*!
			@brief
				Approximate memory the tile holds.
			@return
				Bytes of the struct plus its map arrays
			*/
		std::size_t bytes() const;
	};

	/** A level-0 section that replaces the generated one: a hand edit kept from before the cell was evicted. */
	struct StoredSection
	{
		glm::ivec3				section_	{ 0 };		// in the world's level-0 grid
		edit::SectionSnapshot	snapshot_;
	};

	/**
	 * One cell, ready to install: for each level from detail_ up, the page
	 * World::installPage takes. Levels below detail_ are empty - they were
	 * built, because every level is a mip of the one below, and let go again.
	 *
	 */
	struct CellSections
	{
		glm::ivec2	cell_	{ 0 };
		int			detail_	{ 0 };

		std::array<std::vector<Section>, kLodLevels>		sections_;
		std::array<std::vector<std::uint8_t>, kLodLevels>	masks_;
	};

	class TileGenerator
	{
	public:

		/*!
			@brief
				Derives the tile, pad, pure and blend sizes from an endless recipe and checks them.
			@param recipe
				An endless biome recipe (stream_.endless_); throws std::runtime_error
				otherwise, or when a material is missing
			@param materials
				Material table the tiles resolve names against
			*/
		TileGenerator(const Recipe& recipe, const MaterialTable& materials);

		/*!
			@brief
				The endless recipe.
			@return
				Reference to the recipe
			*/
		const Recipe& recipe() const { return recipe_; }

		/*!
			@brief
				Dimensions of the whole streamed world.
			@return
				The world dimensions
			*/
		WorldDims dims() const { return dims_; }

		/*!
			@brief
				Cells along one side of a tile.
			@return
				recipe().stream_.tileCells_
			*/
		int tileCells() const { return recipe_.stream_.tileCells_; }

		/*!
			@brief
				Voxels along a tile's core.
			@return
				The core size in voxels
			*/
		int coreVoxels() const { return core_; }
		/*!
			@brief
				Voxels of the pad round a tile's core.
			@return
				The pad size in voxels
			*/
		int padVoxels() const { return pad_; }

		/*!
			@brief
				Voxels inside a tile's border that are the uneroded ground exactly.
			@return
				The pure band in voxels
			*/
		int pureVoxels() const { return pure_; }

		/*!
			@brief
				How far, in voxels, the passes after erosion look sideways; pure and pad cover it.
			@return
				The reach in voxels
			*/
		int reachVoxels() const { return reach_; }

		/*!
			@brief
				The tile a cell belongs to.
			@param cell
				Cell coordinates
			@return
				Tile coordinates
			*/
		glm::ivec2 tileOfCell(const glm::ivec2& cell) const { return { cell.x / tileCells(), cell.y / tileCells() }; }

		/*!
			@brief
				Runs the prepass for one tile.

				About as dear as generating a bounded map of the same size;
				single-threaded, because it is meant to be one of several running
				side by side.
			@param tile
				Tile coordinates
			@param cancel
				Polled between passes
			@return
				Null if cancelled
			*/
		std::shared_ptr<const Tile> buildTile(const glm::ivec2& tile, const std::atomic<bool>* cancel = nullptr) const;

		/*!
			@brief
				Fills a cell from its tile and builds its LoD levels.
			@param tile
				tileOfCell(cell)'s
			@param cell
				Cell coordinates
			@param detail
				The finest level to keep, 0..3
			@param edits
				Level-0 sections of this cell that replace what would be generated;
				the coarser levels are built from them, so an edit shows at any distance
			@return
				The cell's pages from detail up
			*/
		CellSections buildCell(const Tile& tile, const glm::ivec2& cell, int detail, std::span<const StoredSection> edits = {}) const;

	private:

		/*!
			@brief
				The generator for one tile: the recipe cut down to the tile's window, with its TileOptions.
			@param tile
				Tile coordinates
			@return
				The tile's generator
			*/
		std::shared_ptr<const TerrainGenerator> buildWindow(const glm::ivec2& tile) const;

		Recipe			recipe_;
		MaterialTable	materials_;
		WorldDims		dims_;

		int		core_	{ 0 };
		int		pad_	{ 0 };
		int		pure_	{ 0 };
		int		blend_	{ 0 };
		int		reach_	{ 0 };

		glm::ivec3				levelSections_[kLodLevels]	{};		// the world's section grid per level
		std::int64_t			originVoxelX_	{ 0 };
		std::int64_t			originVoxelZ_	{ 0 };
	};

	/**
	 * The tiles near the player. A tile is a second or so of work and several
	 * MB, and sixteen cells are filled from it, some of them more than once as
	 * their detail changes: worth keeping, within reason. Over capacity, the
	 * tile furthest from the focus goes (the least recently used among equals):
	 * the near ones are those whose cells are about to get finer.
	 *
	 * Thread-safe. Two ways in: get() builds a missing tile on the caller and
	 * makes a second caller wait for the first; reserve() + build() let the
	 * streamer build tiles as jobs of their own and hand a cell to a worker
	 * only once its tile is ready(), so no worker ever sits waiting on a tile.
	 *
	 */
	class TileCache
	{
	public:

		/*!
			@brief
				An empty cache.
			@param capacity
				Tiles held before eviction starts
			*/
		explicit TileCache(std::size_t capacity = 24) : capacity_(capacity) {}

		/*!
			@brief
				Sets how many tiles are held (at least 1) and evicts down to it.
			@param capacity
				Tiles held before eviction starts
			*/
		void setCapacity(std::size_t capacity);

		/*!
			@brief
				The tile eviction measures from (in tiles).
			@param tile
				Tile coordinates of the focus
			*/
		void setFocus(const glm::ivec2& tile);

		/*!
			@brief
				Returns a tile, building it on the caller if missing or waiting for another caller's build.
			@param generator
				Builds the tile when it is not held
			@param tile
				Tile coordinates
			@param cancel
				Polled by the build
			@return
				The tile, built with the generator if it was not held. Null if the
				build was cancelled
			*/
		std::shared_ptr<const Tile> get(const TileGenerator& generator, const glm::ivec2& tile, const std::atomic<bool>* cancel);

		/*!
			@brief
				Returns a tile only if it is already built. Never waits.
			@param tile
				Tile coordinates
			@return
				The tile if it is held and built (it counts as used), otherwise null
			*/
		std::shared_ptr<const Tile> ready(const glm::ivec2& tile);

		/*!
			@brief
				Claims a tile nobody holds, for a build() to come; until then it reads as being built.
			@param tile
				Tile coordinates
			@return
				False if the tile was held (built or being built) already
			*/
		bool reserve(const glm::ivec2& tile);

		/*!
			@brief
				Builds a reserve()d tile on the calling thread; does nothing for one that was not.

				Rethrows what the build throws.
			@param generator
				Builds the tile
			@param tile
				Tile coordinates
			@param cancel
				Polled by the build
			*/
		void build(const TileGenerator& generator, const glm::ivec2& tile, const std::atomic<bool>* cancel);

		/*!
			@brief
				Drops every tile.
			*/
		void clear();

		/*!
			@brief
				Tiles held, built or being built.
			@return
				The entry count
			*/
		std::size_t size() const;
		/*!
			@brief
				Memory held by the built tiles.
			@return
				Sum of Tile::bytes() over built tiles
			*/
		std::size_t bytes() const;
		/*!
			@brief
				Lookups answered from the cache.
			@return
				The hit count
			*/
		std::uint64_t hits() const { return hits_.load(); }
		/*!
			@brief
				Tile builds started.
			@return
				The build count
			*/
		std::uint64_t builds() const { return builds_.load(); }

	private:

		using Promise = std::promise<std::shared_ptr<const Tile>>;
		using Future = std::shared_future<std::shared_ptr<const Tile>>;

		struct Entry
		{
			glm::ivec2					at_		{ 0 };
			Future						tile_;
			std::shared_ptr<Promise>	promise_;		// until a build takes it
			std::uint64_t				used_	{ 0 };
		};

		/*!
			@brief
				Packs tile coordinates into a map key.
			@param tile
				Tile coordinates
			@return
				y in the high 32 bits, x in the low 32
			*/
		static std::uint64_t key(const glm::ivec2& tile)
		{
			return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(tile.y)) << 32) | static_cast<std::uint32_t>(tile.x);
		}

		/*!
			@brief
				Adds an entry being built; the mutex is held.
			@param tile
				Tile coordinates
			*/
		void insert(const glm::ivec2& tile);

		/*!
			@brief
				Builds the tile and settles the promise; a cancelled or failed build leaves nothing behind.
			@param generator
				Builds the tile
			@param tile
				Tile coordinates
			@param promise
				Settled with the tile, null, or the build's exception
			@param cancel
				Polled by the build
			@return
				The tile, or null if cancelled
			*/
		std::shared_ptr<const Tile> fulfil(const TileGenerator& generator, const glm::ivec2& tile, Promise& promise, const std::atomic<bool>* cancel);

		/*!
			@brief
				Drops built tiles, furthest from the focus first, until the cache is within capacity; the mutex is held.
			*/
		void evict();

		mutable std::mutex							mutex_;
		std::unordered_map<std::uint64_t, Entry>	entries_;
		std::size_t									capacity_;
		glm::ivec2									focus_		{ 0 };
		std::uint64_t								clock_		{ 0 };
		std::atomic<std::uint64_t>					hits_		{ 0 };
		std::atomic<std::uint64_t>					builds_		{ 0 };
	};
}
