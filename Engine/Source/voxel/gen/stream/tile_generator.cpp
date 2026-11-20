/*************************************************************************************************************
\file       tile_generator.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Implements TileGenerator (tile windows, per-tile prepass, cell fill with kept edits and
            LoD mips) and TileCache (promise-based builds, focus-distance eviction).

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "tile_generator.hpp"

// Voxel
#include "../surface_rules.hpp"
#include "../../world/mipper.hpp"

// Profiling
#include "../../../profile/profiler.hpp"

// stds
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <stdexcept>


namespace voxel
{
	namespace
	{
		constexpr int kCellVoxels = World::kCellSections * kSectionSize;

		// The least a tile keeps of identical ground inside its border, and of
		// pad outside it. What the recipe needs (reachVoxels()) is usually less.
		constexpr int kMinBandVoxels = 32;

		// Slack on top of the reach, so a recipe tuned a little does not land
		// exactly on the limit.
		constexpr int kReachSlackVoxels = 8;

		// How wide the fade from uneroded to eroded ground is. Wide enough that
		// the change of character does not read as a line on the ground.
		constexpr int kBlendVoxels = 64;

		// The spike clamp and the detail gate's slope each look one column away.
		constexpr int kLocalPassesVoxels = 2;

		/*!
			@brief
				Rounds a positive value up to a multiple.
			@param value
				Value to round
			@param multiple
				The step
			@return
				The smallest multiple not below value
			*/
		int roundUp(int value, int multiple)
		{
			return (value + multiple - 1) / multiple * multiple;
		}

		/*!
			@brief
				splitmix64 of the tile coordinates: a different rain on every tile, from the tile alone.
			@param tile
				Tile coordinates
			@return
				The erosion salt
			*/
		std::uint64_t tileSalt(const glm::ivec2& tile)
		{
			std::uint64_t z = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(tile.y)) << 32)
				| static_cast<std::uint32_t>(tile.x);
			z += 0x9E3779B97F4A7C15ull;
			z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
			z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
			return z ^ (z >> 31);
		}
	}

	/*!
		@brief
			Approximate memory the tile holds.
		@return
			Bytes of the struct plus its map arrays
		*/
	std::size_t Tile::bytes() const
	{
		return sizeof(Tile)
			+ maps_.surface_.size() * sizeof(std::int16_t) + maps_.waterTop_.size() * sizeof(std::int16_t)
			+ maps_.topMaterial_.size() + maps_.topDepth_.size() + maps_.soilDepth_.size()
			+ maps_.biome_.size() + maps_.strataShift_.size()
			+ maps_.height_.h_.size() * sizeof(float);
	}

	/*!
		@brief
			Derives core, reach, pure, pad and blend from the recipe, and builds one window to fail early.
		@param recipe
			An endless biome recipe; throws std::runtime_error otherwise
		@param materials
			Material table the tiles resolve names against
		*/
	TileGenerator::TileGenerator(const Recipe& recipe, const MaterialTable& materials)
		: recipe_(recipe)
		, materials_(materials)
	{
		if (!recipe_.stream_.endless_ || !recipe_.biomes_.enabled_)
			throw std::runtime_error("TileGenerator: the recipe is not an endless biome terrain");

		dims_ = WorldDims::fromVoxels(recipe_.size_, recipe_.voxelSize_, recipe_.origin_);

		const int cellVoxels = recipe_.erosion_.cellVoxels_;
		core_ = tileCells() * kCellVoxels;

		if (recipe_.size_.x % core_ != 0 || recipe_.size_.z % core_ != 0 || core_ % cellVoxels != 0)
			throw std::runtime_error("TileGenerator: the terrain is not a whole number of tiles, or a tile of erosion cells");

		// The upsample's outer taps are two coarse cells out.
		reach_ = classifyReachVoxels(recipe_) + kLocalPassesVoxels + 2 * cellVoxels;

		const int band = roundUp(std::max(kMinBandVoxels, reach_ + kReachSlackVoxels), cellVoxels);
		pure_ = band;
		pad_ = band;
		blend_ = std::min(kBlendVoxels, core_ / 2 - pure_);

		if (blend_ < 0)
			throw std::runtime_error("TileGenerator: the surface rules reach further than half a tile; use larger tiles");

		glm::ivec3 d = dims_.sections_;
		for (int level = 0; level < kLodLevels; ++level)
		{
			levelSections_[level] = d;
			d = (d + 1) / 2;
		}

		originVoxelX_ = std::llround(static_cast<double>(recipe_.origin_.x) / recipe_.voxelSize_);
		originVoxelZ_ = std::llround(static_cast<double>(recipe_.origin_.z) / recipe_.voxelSize_);

		// Fails here, on the caller's thread, rather than on a worker later.
		(void)buildWindow({ 0, 0 });
	}

	/*!
		@brief
			The generator for one tile: the recipe cut down to the tile's window, with its TileOptions.
		@param tile
			Tile coordinates
		@return
			The tile's generator
		*/
	std::shared_ptr<const TerrainGenerator> TileGenerator::buildWindow(const glm::ivec2& tile) const
	{
		Recipe window = recipe_;
		window.stream_ = {};
		window.size_ = { core_ + 2 * pad_, recipe_.size_.y, core_ + 2 * pad_ };

		// From whole voxels, in double: the window must start on the very
		// lattice point its neighbour counts from.
		const std::int64_t vx = originVoxelX_ + static_cast<std::int64_t>(tile.x) * core_ - pad_;
		const std::int64_t vz = originVoxelZ_ + static_cast<std::int64_t>(tile.y) * core_ - pad_;
		window.origin_.x = static_cast<float>(static_cast<double>(vx) * recipe_.voxelSize_);
		window.origin_.z = static_cast<float>(static_cast<double>(vz) * recipe_.voxelSize_);

		TileOptions options;
		options.erosionSalt_ = tileSalt(tile);
		options.voxelOffset_ = { tile.x * core_ - pad_, tile.y * core_ - pad_ };
		options.core_ = core_;
		options.pad_ = pad_;
		options.pure_ = pure_;
		options.blend_ = blend_;

		return std::make_shared<const TerrainGenerator>(window, materials_, options);
	}

	/*!
		@brief
			Runs the prepass for one tile on the calling thread; the float heights are dropped.
		@param tile
			Tile coordinates
		@param cancel
			Polled between passes
		@return
			The tile, or null if cancelled
		*/
	std::shared_ptr<const Tile> TileGenerator::buildTile(const glm::ivec2& tile, const std::atomic<bool>* cancel) const
	{
		VOX_PROFILE_SCOPE_C("Build terrain tile", profile::colour::kVoxel);

		auto out = std::make_shared<Tile>();
		out->tile_ = tile;
		out->generator_ = buildWindow(tile);

		std::optional<TerrainMaps> maps = out->generator_->buildMaps(nullptr, nullptr, cancel);
		if (!maps)
			return nullptr;

		// Only previews read the float heights; the fill reads the columns.
		maps->height_ = HeightField{};
		out->maps_ = std::move(*maps);
		return out;
	}

	/*!
		@brief
			Fills a cell's level 0 from its tile, overlays kept edits, and mips levels 1..3.
		@param tile
			tileOfCell(cell)'s; throws std::logic_error otherwise
		@param cell
			Cell coordinates
		@param detail
			The finest level to keep, clamped to 0..3
		@param edits
			Level-0 sections that replace the generated ones
		@return
			The cell's pages and child masks from detail up
		*/
	CellSections TileGenerator::buildCell(const Tile& tile, const glm::ivec2& cell, int detail, std::span<const StoredSection> edits) const
	{
		VOX_PROFILE_SCOPE_C("Build terrain cell", profile::colour::kVoxel);

		if (tileOfCell(cell) != tile.tile_)
			throw std::logic_error("TileGenerator::buildCell: the cell is not in this tile");

		detail = std::clamp(detail, 0, kLodLevels - 1);

		CellSections out;
		out.cell_ = cell;
		out.detail_ = detail;

		std::array<std::vector<Section>, kLodLevels> pages;
		std::array<std::vector<std::uint8_t>, kLodLevels> masks;

		// ---------------------------- Level 0 ----------------------------

		const glm::ivec2 local = cell - tile.tile_ * tileCells();
		const int x0 = pad_ + local.x * kCellVoxels, z0 = pad_ + local.y * kCellVoxels;

		const int side0 = World::pageSide(0);
		const int height0 = levelSections_[0].y;
		pages[0].resize(static_cast<std::size_t>(side0) * side0 * height0);

		std::vector<Section> column(static_cast<std::size_t>(height0));
		for (int lz = 0; lz < side0; ++lz)
		for (int lx = 0; lx < side0; ++lx)
		{
			tile.generator_->fillColumnSections(tile.maps_, x0 + lx * kSectionSize, z0 + lz * kSectionSize, column);

			for (int sy = 0; sy < height0; ++sy)
				pages[0][World::pageSlot(0, lx, sy, lz)] = std::move(column[static_cast<std::size_t>(sy)]);
		}

		// What was edited here before the cell was last let go.
		const glm::ivec3 first{ cell.x * side0, 0, cell.y * side0 };
		for (const StoredSection& edit : edits)
		{
			const glm::ivec3 l = edit.section_ - first;
			if (l.x < 0 || l.z < 0 || l.x >= side0 || l.z >= side0 || l.y < 0 || l.y >= height0)
				continue;

			pages[0][World::pageSlot(0, l.x, l.y, l.z)] = edit.snapshot_.restore();
		}

		// --------------------------- Levels 1..3 --------------------------
		//
		// The same rule World::mipOne applies, on pages that belong to no
		// world yet. A child above the level's height is absent, as it is
		// above the world.
		for (int level = 1; level < kLodLevels; ++level)
		{
			const int side = World::pageSide(level);
			const int height = levelSections_[level].y;
			const int below = levelSections_[level - 1].y;

			pages[level].resize(static_cast<std::size_t>(side) * side * height);
			masks[level].assign(pages[level].size(), 0);

			for (int sy = 0; sy < height; ++sy)
			for (int lz = 0; lz < side; ++lz)
			for (int lx = 0; lx < side; ++lx)
			{
				const Section* children[8];
				std::uint8_t mask = 0;

				for (int cx = 0; cx < 2; ++cx)
				for (int cy = 0; cy < 2; ++cy)
				for (int cz = 0; cz < 2; ++cz)
				{
					const int k = childIndex(cx, cy, cz);
					const int y = 2 * sy + cy;

					const Section* child = y < below
						? &pages[level - 1][World::pageSlot(level - 1, 2 * lx + cx, y, 2 * lz + cz)]
						: nullptr;

					children[k] = child;
					if (child && !child->isAir())
						mask |= static_cast<std::uint8_t>(1u << k);
				}

				const std::size_t at = World::pageSlot(level, lx, sy, lz);
				pages[level][at] = mipSection(children, materials_);
				masks[level][at] = mask;
			}
		}

		// Everything finer than asked for goes back to the pool right here.
		for (int level = detail; level < kLodLevels; ++level)
		{
			out.sections_[level] = std::move(pages[level]);
			out.masks_[level] = std::move(masks[level]);
		}

		return out;
	}

	// ================================ Cache =================================

	/*!
		@brief
			Sets how many tiles are held (at least 1) and evicts down to it.
		@param capacity
			Tiles held before eviction starts
		*/
	void TileCache::setCapacity(std::size_t capacity)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		capacity_ = std::max<std::size_t>(capacity, 1);
		evict();
	}

	/*!
		@brief
			The tile eviction measures from (in tiles).
		@param tile
			Tile coordinates of the focus
		*/
	void TileCache::setFocus(const glm::ivec2& tile)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		focus_ = tile;
	}

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
			The tile, or null if the build was cancelled
		*/
	std::shared_ptr<const Tile> TileCache::get(const TileGenerator& generator, const glm::ivec2& tile, const std::atomic<bool>* cancel)
	{
		std::shared_ptr<Promise> promise;

		{
			std::unique_lock<std::mutex> lock(mutex_);

			if (const auto found = entries_.find(key(tile)); found != entries_.end())
			{
				found->second.used_ = ++clock_;
				const Future held = found->second.tile_;
				lock.unlock();

				hits_.fetch_add(1);

				// Ready, or being built by another worker: wait for that one.
				return held.get();
			}

			insert(tile);

			Entry& entry = entries_.at(key(tile));
			promise = std::move(entry.promise_);
			entry.promise_.reset();
		}

		return fulfil(generator, tile, *promise, cancel);
	}

	/*!
		@brief
			Returns a tile only if it is already built; never waits.
		@param tile
			Tile coordinates
		@return
			The tile if held and built (it counts as used), otherwise null
		*/
	std::shared_ptr<const Tile> TileCache::ready(const glm::ivec2& tile)
	{
		std::unique_lock<std::mutex> lock(mutex_);

		const auto found = entries_.find(key(tile));
		if (found == entries_.end() || found->second.tile_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
			return nullptr;

		// A failed or cancelled build is erased before its promise settles, so
		// a ready entry holds a tile.
		found->second.used_ = ++clock_;
		const Future held = found->second.tile_;
		lock.unlock();

		hits_.fetch_add(1);
		return held.get();
	}

	/*!
		@brief
			Claims a tile nobody holds, for a build() to come.
		@param tile
			Tile coordinates
		@return
			False if the tile was held already
		*/
	bool TileCache::reserve(const glm::ivec2& tile)
	{
		std::lock_guard<std::mutex> lock(mutex_);

		if (entries_.count(key(tile)) != 0)
			return false;

		insert(tile);
		return true;
	}

	/*!
		@brief
			Builds a reserve()d tile on the calling thread; does nothing for one that was not.
		@param generator
			Builds the tile
		@param tile
			Tile coordinates
		@param cancel
			Polled by the build
		*/
	void TileCache::build(const TileGenerator& generator, const glm::ivec2& tile, const std::atomic<bool>* cancel)
	{
		std::shared_ptr<Promise> promise;

		{
			std::lock_guard<std::mutex> lock(mutex_);

			const auto found = entries_.find(key(tile));
			if (found == entries_.end() || !found->second.promise_)
				return;

			promise = std::move(found->second.promise_);
			found->second.promise_.reset();
		}

		fulfil(generator, tile, *promise, cancel);
	}

	/*!
		@brief
			Adds an entry being built, with a fresh promise, then evicts; the mutex is held.
		@param tile
			Tile coordinates
		*/
	void TileCache::insert(const glm::ivec2& tile)
	{
		Entry entry;
		entry.at_ = tile;
		entry.promise_ = std::make_shared<Promise>();
		entry.tile_ = entry.promise_->get_future().share();
		entry.used_ = ++clock_;

		entries_[key(tile)] = std::move(entry);
		evict();
	}

	/*!
		@brief
			Builds the tile and settles the promise; a cancelled or failed build erases its entry.
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
	std::shared_ptr<const Tile> TileCache::fulfil(const TileGenerator& generator, const glm::ivec2& tile, Promise& promise, const std::atomic<bool>* cancel)
	{
		builds_.fetch_add(1);

		std::shared_ptr<const Tile> built;
		try
		{
			built = generator.buildTile(tile, cancel);
		}
		catch (...)
		{
			// Gone before the promise settles: while it is unsettled nothing
			// else can have replaced the entry, so this erases our own.
			{
				std::lock_guard<std::mutex> lock(mutex_);
				entries_.erase(key(tile));
			}

			promise.set_exception(std::current_exception());
			throw;
		}

		// A cancelled build is not a tile; the next asker builds it again.
		if (!built)
		{
			std::lock_guard<std::mutex> lock(mutex_);
			entries_.erase(key(tile));
		}

		promise.set_value(built);
		return built;
	}

	/*!
		@brief
			Drops built tiles, furthest from the focus first (oldest among equals), until within capacity.
		*/
	void TileCache::evict()
	{
		const auto distance = [this](const Entry& e)
		{
			return std::max(std::abs(e.at_.x - focus_.x), std::abs(e.at_.y - focus_.y));
		};

		while (entries_.size() > capacity_)
		{
			auto victim = entries_.end();
			for (auto it = entries_.begin(); it != entries_.end(); ++it)
			{
				// One still being built has someone waiting on it.
				if (it->second.tile_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
					continue;

				if (victim == entries_.end())
				{
					victim = it;
					continue;
				}

				// Furthest from the focus first; among equals the oldest.
				const int d = distance(it->second), dv = distance(victim->second);
				if (d > dv || (d == dv && it->second.used_ < victim->second.used_))
					victim = it;
			}

			if (victim == entries_.end())
				return;

			entries_.erase(victim);
		}
	}

	/*!
		@brief
			Drops every tile.
		*/
	void TileCache::clear()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		entries_.clear();
	}

	/*!
		@brief
			Tiles held, built or being built.
		@return
			The entry count
		*/
	std::size_t TileCache::size() const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return entries_.size();
	}

	/*!
		@brief
			Memory held by the built tiles; unbuilt and failed entries count nothing.
		@return
			Sum of Tile::bytes() over built tiles
		*/
	std::size_t TileCache::bytes() const
	{
		std::lock_guard<std::mutex> lock(mutex_);

		std::size_t total = 0;
		for (const auto& [k, entry] : entries_)
		{
			if (entry.tile_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
				continue;

			// A build that threw is ready too, and has nothing to count.
			try
			{
				if (const std::shared_ptr<const Tile> tile = entry.tile_.get())
					total += tile->bytes();
			}
			catch (...)
			{
			}
		}
		return total;
	}
}
