/*************************************************************************************************************
\file       voxel_stream_self_check.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Startup self-check for endless terrain streaming: the endless recipe, tiles as pure functions,
            matching seams, worker-built cell levels, kept edits, the tile cache, the edit store and
            CellStreamer against a sparse world.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "voxel_stream_self_check.hpp"

// Voxel
#include "../../voxel/types/material_table.hpp"
#include "../../voxel/world/world.hpp"
#include "../../voxel/gen/recipe.hpp"
#include "../../voxel/gen/terrain_generator.hpp"
#include "../../voxel/gen/biome/biome_library.hpp"
#include "../../voxel/gen/stream/tile_generator.hpp"
#include "../../voxel/stream/cell_streamer.hpp"
#include "../../voxel/stream/edit_store.hpp"

// stds
#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>


namespace engine::checks
{
	namespace
	{
		using namespace voxel;

		// Small and quick to erode: the checks are about structure, not looks.
		// Three biomes, so the seam runs through a blend as well as a plain.
		constexpr const char* kLibrary = R"({
			"version": 1,
			"generate": { "dropletsPerSquareMetre": 6, "shellDepth": 2.0 },
			"defaults": {
				"erosion": { "thermal": { "iterations": 8 } },
				"water": { "enabled": true, "minArea": 0.5, "minDepth": 0.2, "maxDepth": 1.0 },
				"surface": { "base": "stone" }
			},
			"biomes": [
				{ "name": "flat", "colour": [10, 200, 30], "base": 0.0, "erodibility": 0.4, "talusDeg": 45,
				  "layers": [ { "op": "fbm", "wavelength": 9, "amplitude": 0.8, "octaves": 3 } ] },
				{ "name": "hills", "colour": [90, 160, 60], "base": 1.0, "erodibility": 0.5, "talusDeg": 40,
				  "layers": [ { "op": "ridged", "wavelength": 14, "amplitude": 3.5, "octaves": 3 } ] },
				{ "name": "mesa", "colour": [180, 100, 60], "base": 0.5, "edgeRelief": 0.3, "erodibility": 0.2, "talusDeg": 78,
				  "layers": [ { "op": "plateau", "wavelength": 8, "amplitude": 3, "octaves": 2 } ],
				  "palette": { "top": "sandstone", "under": "sandstone", "rock": "redrock", "sand": "sand", "snow": "snow" },
				  "strata": { "wobble": 0.4, "bands": [ { "material": "redrock", "thickness": 0.5 }, { "material": "sandstone", "thickness": 0.3 } ] },
				  "rules": { "soilDepth": 0.3 } }
			]
		})";

		/*!
			@brief
				Compares every per-column field of two sets of terrain maps.
			@param a
				First maps
			@param b
				Second maps
			@return
				True when surface, water, materials, depths, biome and strata all match
			*/
		bool sameColumns(const TerrainMaps& a, const TerrainMaps& b)
		{
			return a.surface_ == b.surface_ && a.waterTop_ == b.waterTop_ && a.topMaterial_ == b.topMaterial_
				&& a.topDepth_ == b.topDepth_ && a.soilDepth_ == b.soilDepth_ && a.biome_ == b.biome_
				&& a.strataShift_ == b.strataShift_;
		}

		/*!
			@brief
				Compares two sections' voxels, including whether each is uniform.
			@param a
				First section
			@param b
				Second section
			@return
				True when both are uniform with the same material or dense with the
				same bytes
			*/
		bool sameSection(const Section& a, const Section& b)
		{
			if (a.isUniform() != b.isUniform())
				return false;
			if (a.isUniform())
				return a.uniformMaterial() == b.uniformMaterial();
			return std::memcmp(a.data(), b.data(), static_cast<std::size_t>(kSectionVolume)) == 0;
		}
	}

	/*!
		@brief
			Exercises terrain streaming's pure pieces and prints
			"[voxel-stream-check]" results. Never throws.
		*/
	void runVoxelStreamSelfCheck()
	{
		int failures = 0;

		const auto check = [&failures](bool ok, const char* what)
		{
			if (!ok)
			{
				std::cerr << "[voxel-stream-check] FAILED: " << what << "\n";
				++failures;
			}
		};

		try
		{
			const MaterialTable materials = MaterialTable::builtin();
			const std::string json = kLibrary;
			const BiomeLibrary library = BiomeLibrary::parse(std::span(reinterpret_cast<const unsigned char*>(json.data()), json.size()));

			// ---------------------------- Recipe ----------------------------

			GenerateSettings settings;
			settings.size_ = { 500, 128, 300 };		// rounds up to whole tiles: 512 x 512
			settings.origin_ = { -25.6f, 0.f, -25.6f };
			settings.biomes_ = { "flat", "hills", "mesa" };
			settings.biomeSize_ = 12.f;
			settings.blending_ = 0.4f;
			settings.seed_ = 77;
			settings.endless_ = true;
			settings.tileCells_ = 1;

			const Recipe recipe = composeBiomeRecipe(library, settings);
			check(recipe.stream_.endless_ && recipe.stream_.tileCells_ == 1 && recipe.size_ == glm::ivec3{ 512, 128, 512 },
				"an endless recipe's extent is not rounded up to whole tiles");
			check(recipe.erosion_.minRelief_ > 0.f && recipe.erosion_.cellVoxels_ == 2,
				"an endless recipe does not pin the erosion's relief and cell size");

			const std::string text = recipe.toJson();
			check(Recipe::parse(std::span(reinterpret_cast<const unsigned char*>(text.data()), text.size())) == recipe,
				"an endless recipe does not survive toJson() and parse()");

			bool refused = false;
			try { TerrainGenerator whole(recipe, materials); }
			catch (const std::exception&) { refused = true; }
			check(refused, "the whole-map generator accepted an endless recipe");

			{
				Recipe shaped = recipe;
				shaped.biomes_ = {};
				const std::string bad = shaped.toJson();
				bool rejected = false;
				try { Recipe::parse(std::span(reinterpret_cast<const unsigned char*>(bad.data()), bad.size())); }
				catch (const std::exception&) { rejected = true; }
				check(rejected, "a shaped recipe was accepted as endless");
			}

			// ----------------------------- Tiles ----------------------------

			const TileGenerator tiles(recipe, materials);
			const int core = tiles.coreVoxels(), pad = tiles.padVoxels();

			check(core == 256 && tiles.dims().sections_ == glm::ivec3{ 16, 4, 16 }, "tile or world dimensions are off");
			check(tiles.pureVoxels() >= tiles.reachVoxels() && pad >= tiles.reachVoxels(),
				"a tile's identical band is narrower than the surface rules reach");

			const std::shared_ptr<const Tile> a = tiles.buildTile({ 0, 0 });
			const std::shared_ptr<const Tile> again = tiles.buildTile({ 0, 0 });
			const std::shared_ptr<const Tile> b = tiles.buildTile({ 1, 0 });
			const std::shared_ptr<const Tile> c = tiles.buildTile({ 0, 1 });

			check(a && again && sameColumns(a->maps_, again->maps_), "a tile is not a function of (recipe, tile)");
			check(a && b && !sameColumns(a->maps_, b->maps_), "two different tiles came out the same");

			// Along a seam, both tiles computed the columns on either side of
			// it: one as the edge of its core, the other in its pad. The same
			// world column must have come out the same - that, and not a bound
			// on the step, is what "no seam" means, because then the join is
			// as continuous as the inside of a tile. Compared a margin short
			// of the window's own edge, where a filter clamps.
			const int margin = tiles.reachVoxels();
			const int span = core + 2 * pad;
			const auto seamAgrees = [&](const Tile& left, const Tile& right, bool alongX)
			{
				for (int along = margin; along < span - margin; ++along)
				for (int across = -(pad - margin); across < pad - margin; ++across)
				{
					// `across` counts columns from the seam; negative is the left tile's core.
					const int inLeft = pad + core + across, inRight = pad + across;
					const std::size_t i = alongX ? left.maps_.index(inLeft, along) : left.maps_.index(along, inLeft);
					const std::size_t j = alongX ? right.maps_.index(inRight, along) : right.maps_.index(along, inRight);

					if (left.maps_.surface_[i] != right.maps_.surface_[j]
						|| left.maps_.topMaterial_[i] != right.maps_.topMaterial_[j]
						|| left.maps_.topDepth_[i] != right.maps_.topDepth_[j]
						|| left.maps_.soilDepth_[i] != right.maps_.soilDepth_[j]
						|| left.maps_.waterTop_[i] != right.maps_.waterTop_[j]
						|| left.maps_.biome_[i] != right.maps_.biome_[j]
						|| left.maps_.strataShift_[i] != right.maps_.strataShift_[j])
						return false;
				}
				return true;
			};

			check(a && b && seamAgrees(*a, *b, true), "two tiles disagree about the ground along their x seam");
			check(a && c && seamAgrees(*a, *c, false), "two tiles disagree about the ground along their z seam");

			// And the middle of a tile is eroded: it is not all base.
			if (a)
			{
				bool wet = false, varied = false;
				const std::size_t mid = a->maps_.index(span / 2, span / 2);
				for (std::size_t i = 0; i < a->maps_.surface_.size(); ++i)
				{
					wet = wet || a->maps_.waterTop_[i] >= 0;
					varied = varied || a->maps_.surface_[i] != a->maps_.surface_[mid];
				}
				check(varied, "a tile's ground is flat");
				(void)wet;	// lakes are allowed, not required
			}

			// ----------------------------- Cells ----------------------------

			if (a)
			{
				const CellSections fine = tiles.buildCell(*a, { 0, 0 }, 0);
				const CellSections coarse = tiles.buildCell(*a, { 0, 0 }, 2);

				check(fine.sections_[0].size() == 8u * 8u * 4u && fine.sections_[1].size() == 4u * 4u * 2u
					&& fine.sections_[2].size() == 2u * 2u * 1u && fine.sections_[3].size() == 1u,
					"a cell's pages are not the size World::installPage expects");
				check(coarse.sections_[0].empty() && coarse.sections_[1].empty() && !coarse.sections_[2].empty(),
					"a cell built at detail 2 kept finer levels");

				bool sameCoarse = coarse.sections_[2].size() == fine.sections_[2].size();
				for (int level = 2; level < kLodLevels && sameCoarse; ++level)
					for (std::size_t i = 0; i < fine.sections_[level].size() && sameCoarse; ++i)
						sameCoarse = sameSection(fine.sections_[level][i], coarse.sections_[level][i])
							&& fine.masks_[level][i] == coarse.masks_[level][i];
				check(sameCoarse, "a cell's coarse levels depend on the detail it was built at");

				// The levels a World builds from the same level 0 are the
				// reference: a one-cell bounded world.
				World reference(WorldDims::fromVoxels({ 256, 128, 256 }, recipe.voxelSize_, glm::vec3{ 0.f }));
				bool solid = false;
				for (int sy = 0; sy < 4; ++sy)
				for (int lz = 0; lz < 8; ++lz)
				for (int lx = 0; lx < 8; ++lx)
				{
					const Section& s = fine.sections_[0][World::pageSlot(0, lx, sy, lz)];
					solid = solid || !s.isAir();
					reference.editSection(0, lx, sy, lz) = s.clone();
				}
				reference.rebuildLevels(materials);
				check(solid, "a filled cell is empty");

				bool mips = true;
				for (int level = 1; level < kLodLevels && mips; ++level)
				{
					const glm::ivec3 d = reference.sectionsAt(level);
					for (int sy = 0; sy < d.y && mips; ++sy)
					for (int lz = 0; lz < d.z && mips; ++lz)
					for (int lx = 0; lx < d.x && mips; ++lx)
					{
						const std::size_t at = World::pageSlot(level, lx, sy, lz);
						mips = sameSection(reference.section(level, lx, sy, lz), fine.sections_[level][at])
							&& reference.childMask(level, lx, sy, lz) == fine.masks_[level][at];
					}
				}
				check(mips, "a cell's LoD levels are not what World::rebuildLevels builds from its level 0");

				// A kept edit replaces the generated section and shows in the mips.
				const MaterialId marker = materials.id("ice");
				StoredSection edit;
				edit.section_ = { 3, 3, 5 };	// the top layer: air in this terrain
				edit.snapshot_ = edit::SectionSnapshot::capture(Section::uniform(marker));

				const CellSections edited = tiles.buildCell(*a, { 0, 0 }, 0, std::span(&edit, 1));
				check(sameSection(edited.sections_[0][World::pageSlot(0, 3, 3, 5)], Section::uniform(marker)),
					"a kept edit did not replace the generated section");
				check(edited.sections_[3][0].get(3 * 4 + 1, 3 * 4 + 1, 5 * 4 + 1) == marker,
					"a kept edit did not reach the coarsest level");
				check(sameSection(edited.sections_[0][World::pageSlot(0, 0, 0, 0)], fine.sections_[0][World::pageSlot(0, 0, 0, 0)]),
					"a kept edit changed a section it does not name");

				// Installed in a streamed world, the cell reads back by voxel.
				World streamed(tiles.dims(), World::Storage::Sparse);
				CellSections install = tiles.buildCell(*a, { 0, 0 }, 0);
				const MaterialId bottom = install.sections_[0][World::pageSlot(0, 2, 0, 2)].get(5, 5, 5);
				for (int level = 0; level < kLodLevels; ++level)
					streamed.installPage(level, 0, 0, std::move(install.sections_[level]), std::move(install.masks_[level]));

				check(streamed.get(2 * 32 + 5, 5, 2 * 32 + 5) == bottom && bottom != kAir && streamed.get(300, 5, 5) == kAir
					&& streamed.childMask(3, 0, 0, 0) != 0,
					"an installed cell does not read back, or its neighbour is not air");
			}

			// ----------------------------- Cache ----------------------------
			{
				TileCache cache(2);
				const auto first = cache.get(tiles, { 0, 0 }, nullptr);
				const auto second = cache.get(tiles, { 0, 0 }, nullptr);
				check(first && first == second && cache.builds() == 1 && cache.hits() == 1, "the tile cache built a tile it held");

				cache.get(tiles, { 1, 0 }, nullptr);
				cache.get(tiles, { 0, 0 }, nullptr);	// now the most recent
				cache.get(tiles, { 1, 1 }, nullptr);	// pushes out (1, 0)
				check(cache.size() == 2, "the tile cache grew past its capacity");

				std::uint64_t builds = cache.builds();
				cache.get(tiles, { 0, 0 }, nullptr);
				check(cache.builds() == builds, "the tile cache dropped the tile used last instead of the oldest");

				// Distance beats recency: (0, 0) was used last but is further out.
				cache.setFocus({ 1, 1 });
				cache.get(tiles, { 1, 0 }, nullptr);
				builds = cache.builds();
				cache.get(tiles, { 1, 1 }, nullptr);
				check(cache.builds() == builds && cache.size() == 2, "the tile cache did not drop the tile furthest from the focus");

				// The streamer's way in: reserve, build elsewhere, then ready().
				check(!cache.ready({ 0, 1 }) && cache.reserve({ 0, 1 }) && !cache.reserve({ 0, 1 }) && !cache.ready({ 0, 1 }),
					"a reserved tile reads as absent, or can be reserved twice");
				cache.build(tiles, { 0, 1 }, nullptr);
				const auto reserved = cache.ready({ 0, 1 });
				check(reserved && reserved->tile_ == glm::ivec2{ 0, 1 }, "a reserved tile is not ready once built");
			}

			// --------------------------- Edit store --------------------------
			{
				const MaterialId marker = materials.id("ice");
				stream::EditStore store(std::size_t{ 1 } << 20);

				store.put(11, { 3, 1, 5 }, edit::SectionSnapshot::capture(Section::uniform(marker)));
				store.put(12, { 9, 0, 2 }, edit::SectionSnapshot::capture(Section::uniform(marker)));
				store.put(11, { 3, 1, 5 }, edit::SectionSnapshot::capture(Section::uniform(kAir)));

				const std::vector<StoredSection> here = store.collect({ 0, 0 });
				check(store.size() == 2 && here.size() == 1 && here[0].section_ == glm::ivec3{ 3, 1, 5 } && here[0].snapshot_.restore().isAir(),
					"the edit store does not keep the latest edit of a section, by cell");

				store.erase(11);
				check(store.size() == 1 && !store.contains(11) && store.collect({ 0, 0 }).empty(), "an edit taken back is still kept");

				// Over budget, the oldest goes and the newest stays.
				stream::EditStore small(1);
				small.put(1, { 0, 0, 0 }, edit::SectionSnapshot::capture(Section::uniform(marker)));
				small.put(2, { 1, 0, 0 }, edit::SectionSnapshot::capture(Section::uniform(marker)));
				check(small.size() == 1 && small.contains(2) && small.forgotten() == 1, "the edit store does not forget its oldest edit first");
			}

			// ---------------------------- Streamer ---------------------------
			//
			// The scheduler and its workers against a streamed World, with this
			// loop standing in for voxel::System's write window: let go, put
			// in, again until nothing moves. No meshes, bodies or GL.
			{
				stream::Settings s;
				s.radius_ = { 6.f, 20.f, 30.f, 38.f };
				s.hysteresis_ = 4.f;
				s.workers_ = 4;
				s.tileCacheTiles_ = 6;

				{
					stream::CellStreamer streamer(recipe, materials, s);
					stream::EditStore store;
					World world(streamer.generator().dims(), World::Storage::Sparse);

					const auto pump = [&]
					{
						for (int spin = 0; spin < 4000; ++spin)
						{
							streamer.schedule(store);

							for (const stream::CellStreamer::Eviction& e : streamer.evictions(false))
							{
								for (int level = e.from_; level < std::min(e.to_, static_cast<int>(kLodLevels)); ++level)
									world.removePage(level, e.cell_.x, e.cell_.y);
								streamer.setResident(e.cell_, e.to_);
							}

							for (CellSections& built : streamer.takeFinished(100000))
							{
								for (int level = kLodLevels - 1; level >= built.detail_; --level)
									world.installPage(level, built.cell_.x, built.cell_.y, std::move(built.sections_[static_cast<std::size_t>(level)]),
										std::move(built.masks_[static_cast<std::size_t>(level)]));
								streamer.setResident(built.cell_, built.detail_);
							}

							if (streamer.idle() && streamer.evictions(false).empty())
								return true;

							std::this_thread::sleep_for(std::chrono::milliseconds(2));
						}
						return false;
					};

					// The world is 2 x 2 cells of 25.6 m round the origin's far corner.
					const glm::vec3 origin = streamer.generator().dims().origin_;
					const glm::vec3 inFirst = origin + glm::vec3{ 12.8f, 0.f, 12.8f };

					streamer.setFocus(inFirst);
					check(pump(), "streaming did not settle");
					check(streamer.resident({ 0, 0 }) == 0 && world.hasPage(0, 0, 0) && world.hasPage(3, 0, 0),
						"the cell under the focus is not resident at full detail");
					check(streamer.resident({ 1, 1 }) > 0 && streamer.resident({ 1, 1 }) != stream::CellStreamer::kAbsent
						&& !world.hasPage(0, 1, 1) && world.hasPage(3, 1, 1),
						"a cell further off does not hold just its coarse levels");
					check(streamer.ringResident(0) && streamer.ringResident(3), "the rings round the focus are not all in");

					// Walk to the opposite cell: it comes in, the first one thins out.
					streamer.setFocus(origin + glm::vec3{ 38.4f, 0.f, 38.4f });
					check(pump(), "streaming did not settle after the focus moved");
					check(streamer.resident({ 1, 1 }) == 0 && world.hasPage(0, 1, 1), "the cell the focus moved into did not come in");
					check(streamer.resident({ 0, 0 }) > 0 && !world.hasPage(0, 0, 0) && world.hasPage(3, 0, 0),
						"the cell the focus left still holds level 0, or lost its coarse levels");

					// Out of range altogether: everything goes.
					streamer.setFocus(origin + glm::vec3{ -500.f, 0.f, -500.f });
					check(pump(), "streaming did not settle with the focus out of range");
					check(world.pageCount(0) == 0 && world.pageCount(3) == 0 && streamer.resident({ 1, 1 }) == stream::CellStreamer::kAbsent,
						"cells out of range were not let go");
				}

			}
		}
		catch (const std::exception& e)
		{
			std::cerr << "[voxel-stream-check] threw: " << e.what() << "\n";
			++failures;
		}

		if (failures == 0)
			std::cout << "[voxel-stream-check] all checks passed\n";
		else
			std::cerr << "[voxel-stream-check] SOME CHECKS FAILED\n";
	}
}
