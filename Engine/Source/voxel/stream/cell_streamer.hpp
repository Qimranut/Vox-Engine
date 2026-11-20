/*************************************************************************************************************
\file       cell_streamer.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Streaming settings (stream.json) and CellStreamer, which decides which cells of an endless
            terrain are resident at what detail and builds them on its own worker threads.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Voxel
#include "edit_store.hpp"
#include "../gen/recipe.hpp"
#include "../gen/stream/tile_generator.hpp"
#include "../types/material_table.hpp"

// Threads
#include "../../util/thread/job_queue.hpp"

// GLM
#include <glm/glm.hpp>

// stds
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <unordered_map>
#include <vector>


namespace vox { class ResourceManager; }

namespace voxel::stream
{
	/**
	 * Assets/voxel/stream.json. Distances are metres from the focus (the
	 * player, or the camera) to the nearest point of a cell.
	 *
	 */
	struct Settings
	{
		// How close a cell has to be to hold LoD level 0, 1, 2, 3. Each ring
		// costs the one before it eight times over in voxels, so the fine ones
		// are what to shrink when memory is short. Beyond the last, nothing.
		std::array<float, kLodLevels>	radius_		{ 64.f, 128.f, 192.f, 256.f };

		// A cell is only let go this much further out than it was brought in,
		// so pacing along a ring's edge does not build and drop it over and over.
		float		hysteresis_		{ 16.f };

		// Terrain has collision within this distance, and only there: cooking
		// a collision mesh is the dear half of meshing a section.
		float		colliderRadius_	{ 48.f };

		// The section pool may hold this much before the rings are pulled in.
		std::size_t	poolBudgetBytes_	{ std::size_t{ 512 } << 20 };

		// Hand edits of evicted cells kept for when the player comes back.
		std::size_t	editStoreBytes_		{ std::size_t{ 64 } << 20 };

		// Tiles kept after their cells were built (about 10 MB each): a cell is
		// built again whenever it crosses a ring, from the same tile. The rings'
		// default reach touches some 30-36 tiles; fewer than that and a tile can
		// be dropped between two crossings and built again. The furthest go first.
		std::size_t	tileCacheTiles_		{ 40 };

		// Threads that build tiles and cells; 0 picks half the machine's. At
		// most all but one build tiles at once, so cells whose tile is ready
		// keep coming in.
		unsigned	workers_			{ 0 };

		// Mesh jobs' worth of cells installed a frame - sections that are not
		// air: while a brush stroke is waiting for the same write window, and
		// otherwise.
		int			installJobsEditing_	{ 96 };
		int			installJobs_		{ 512 };

		// The renderer's quad arena for a streamed world; four rings hold
		// several times a bounded map's quads.
		std::size_t	arenaBytes_			{ std::size_t{ 192 } << 20 };

		/*!
			@brief
				Reads voxel/stream.json; a missing or broken file logs and keeps the defaults.
			@param resources
				Resource manager to load the file through
			@return
				The settings
			*/
		static Settings load(vox::ResourceManager& resources);
	};

	/**
	 * Decides which cells of a streamed terrain should be resident and at what
	 * detail, builds them on its own worker threads, and hands the results to
	 * voxel::System, which is the only one that touches the World.
	 *
	 * A cell's DETAIL is the finest LoD level it holds: 0 near the focus, up to
	 * 3 far away, kAbsent beyond. Every cell holds level 3, so there is always
	 * something to draw; the finer levels come and go as the focus moves. A
	 * cell is built whole on a worker - filled at level 0, mipped up, and cut
	 * down to the levels wanted (gen/stream/tile_generator.hpp) - so going
	 * finer is a rebuild, and going coarser is just dropping pages.
	 *
	 * The workers are this class's own, not voxel::System's: that queue going
	 * idle is what opens the World's write window and what "ready" and "can
	 * save" mean, and a tile takes seconds.
	 *
	 * Main thread only, except the jobs it runs.
	 *
	 */
	class CellStreamer
	{
	public:

		static constexpr int kAbsent = kLodLevels;

		/** A cell that holds finer levels than it should any more. to_ == kAbsent lets the whole cell go. */
		struct Eviction
		{
			glm::ivec2	cell_	{ 0 };
			int			from_	{ 0 };
			int			to_		{ kAbsent };
		};

		struct Stats
		{
			std::array<std::uint32_t, kLodLevels>	cells_	{};		// resident, by detail
			std::uint32_t	building_		{ 0 };
			std::uint32_t	waiting_		{ 0 };	// built, not installed yet
			std::size_t		tiles_			{ 0 };
			std::size_t		tileBytes_		{ 0 };
			std::uint64_t	tileBuilds_		{ 0 };
			std::uint64_t	tileHits_		{ 0 };
			float			radiusScale_	{ 1.f };
		};

		/*!
			@brief
				Sets up the tile generator, tile cache and worker threads for an endless recipe.

				Throws std::runtime_error when the recipe is not an endless biome terrain.
			@param recipe
				Endless version-3 recipe
			@param materials
				Material table
			@param settings
				Streaming settings
			*/
		CellStreamer(const Recipe& recipe, const MaterialTable& materials, const Settings& settings);

		/*!
			@brief
				Cancels what is being built and waits for it.
			*/
		~CellStreamer();

		CellStreamer(const CellStreamer&) = delete;
		CellStreamer& operator=(const CellStreamer&) = delete;

		/*!
			@brief
				The tile generator cells are built with.
			@return
				The generator
			*/
		const TileGenerator& generator() const { return generator_; }

		/*!
			@brief
				The settings this streamer was made with.
			@return
				The settings
			*/
		const Settings& settings() const { return settings_; }

		/*!
			@brief
				World position the rings are centred on.
			@param position
				New focus in world space
			*/
		void setFocus(const glm::vec3& position) { focus_ = position; }

		/*!
			@brief
				World position the rings are centred on.
			@return
				The focus
			*/
		const glm::vec3& focus() const { return focus_; }

		/*!
			@brief
				Starts building the cells that should be finer than they are, nearest first.

				Goes as far as the workers have room. A cell whose tile is not
				ready waits for the next call; its tile is built as a job of its
				own meanwhile, so no worker blocks on one.
			@param edits
				What to put in place of generated sections; copied per cell
			*/
		void schedule(const EditStore& edits);

		/*!
			@brief
				Cells that hold finer levels than their distance now calls for.
			@param keepFinest
				Leave detail-0 cells alone: an edit transaction is open, and its
				history must find the sections it touched
			@return
				One eviction per such cell
			*/
		std::vector<Eviction> evictions(bool keepFinest) const;

		/*!
			@brief
				Finished cells, nearest first, that are still an improvement on what is resident.
			@param budgetSections
				Stops after the cell that takes the sections handed out past this;
				always hands out at least one
			@return
				The cells to install
			*/
		std::vector<CellSections> takeFinished(std::size_t budgetSections);

		/*!
			@brief
				Told by whoever installed or removed pages. kAbsent forgets the cell.
			@param cell
				Cell coordinate
			@param detail
				Finest level now resident, or kAbsent
			*/
		void setResident(const glm::ivec2& cell, int detail);

		/*!
			@brief
				The finest level a cell holds.
			@param cell
				Cell coordinate
			@return
				The resident detail, or kAbsent
			*/
		int resident(const glm::ivec2& cell) const;

		/*!
			@brief
				A result being built for this cell is out of date (its kept edits changed).
			@param cell
				Cell coordinate
			*/
		void invalidate(const glm::ivec2& cell);

		/*!
			@brief
				Nothing building, nothing waiting, nothing that should be finer.
			@return
				true when idle as of the last schedule()
			*/
		bool idle() const { return idle_; }

		/*!
			@brief
				Whether every cell that should be at this detail or finer, is.
			@param detail
				LoD level of the ring
			@return
				true when the ring is resident
			*/
		bool ringResident(int detail) const;

		/*!
			@brief
				Distance in metres from the focus to the nearest point of a cell, in the ground plane.
			@param cell
				Cell coordinate
			@return
				Distance in metres
			*/
		float distance(const glm::ivec2& cell) const;

		/*!
			@brief
				Pulls the rings in (< 1) or lets them back out; the memory cap's lever.
			@param scale
				Factor applied to every ring radius
			*/
		void setRadiusScale(float scale) { radiusScale_ = scale; }

		/*!
			@brief
				The factor applied to every ring radius.
			@return
				The radius scale
			*/
		float radiusScale() const { return radiusScale_; }

		/*!
			@brief
				Collects cell counts by detail, cells building and the tile cache's figures.
			@return
				The stats
			*/
		Stats stats() const;

	private:

		struct State
		{
			std::int8_t		resident_	{ static_cast<std::int8_t>(kAbsent) };
			std::int8_t		building_	{ -1 };		// the detail being built, or -1
			std::uint32_t	ticket_		{ 0 };		// of the build in flight
		};

		struct Finished
		{
			CellSections	cell_;
			std::uint32_t	ticket_	{ 0 };
		};

		/*!
			@brief
				Packs a cell coordinate into a 64-bit map key (z high, x low).
			@param cell
				Cell coordinate
			@return
				The key
			*/
		static std::uint64_t key(const glm::ivec2& cell)
		{
			return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cell.y)) << 32) | static_cast<std::uint32_t>(cell.x);
		}

		/*!
			@brief
				The detail a cell at this distance should have; grace widens every ring.
			@param distance
				Distance from the focus in metres
			@param grace
				Metres added to every ring radius
			@return
				The wanted detail, or kAbsent beyond the last ring
			*/
		int wanted(float distance, float grace) const;

		/*!
			@brief
				Calls visit for every cell within the outermost ring's square reach of the focus.
			@param visit
				Called as visit(glm::ivec2 cell)
			*/
		template <typename Visit>
		void forCellsInRange(Visit&& visit) const;

		Settings		settings_;
		TileGenerator	generator_;
		TileCache		tiles_;
		glm::ivec2		cellCount_	{ 0 };
		float			cellMetres_	{ 25.6f };

		glm::vec3		focus_			{ 0.f };
		float			radiusScale_	{ 1.f };
		bool			idle_			{ false };

		std::unordered_map<std::uint64_t, State>	cells_;
		std::uint32_t								nextTicket_	{ 1 };
		std::size_t									inFlight_	{ 0 };		// cells
		std::atomic<std::size_t>					tilesBuilding_	{ 0 };	// counted down by the job

		std::mutex				finishedMutex_;
		std::vector<Finished>	finished_;

		std::atomic<bool>		cancel_	{ false };

		// Last: destroyed first, so no job outlives what it reads.
		util::thread::JobQueue	jobs_;
	};
}
