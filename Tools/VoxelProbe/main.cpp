/*************************************************************************************************************
\file       main.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      VoxelProbe: generates a voxel map end to end without opening a window.
            Usage: VoxelProbe <assetRoot> [recipe] (default maps/arena01.voxgen), or VoxelProbe <assetRoot>
            --biomes arctic,canyons [--size X Y Z] [--origin X Y Z] [--seed N] [--blending 0..1]
            [--biome-size METRES] [--out maps/biomes_demo.voxgen], which composes a biome recipe from
            Assets/voxel/biomes.json the way the Editor's Terrain panel does, writes it to --out and carries on
            from there (sizes in voxels, origin in metres). Generates, mips, meshes and writes the .voxmap
            beside the recipe, prints timings, section counts, cover shares, lake area and step/spike counts,
            and writes a top-down hillshaded preview to build/voxel_probe/preview.png (under the temp folder
            when there is no build folder beside the asset root). A developer tool: it lives outside Engine
            because stb_image_write is tools-only under the approved-library list.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

// Voxel
#include "voxel/types/material_table.hpp"
#include "voxel/gen/recipe.hpp"
#include "voxel/gen/biome/biome_library.hpp"
#include "voxel/gen/terrain_generator.hpp"
#include "voxel/gen/stream/tile_generator.hpp"
#include "voxel/gen/preview/stage_preview.hpp"
#include "voxel/edit/brush_ops.hpp"
#include "voxel/edit/voxel_region.hpp"
#include "voxel/world/world.hpp"
#include "voxel/mesh/section_mesher.hpp"
#include "voxel/io/voxmap_io.hpp"

// Resources
#include "resources/ResourceManager.hpp"

// Threads
#include "util/thread/job_queue.hpp"

// stb
#include <stb_image_write.h>

// stds
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>


namespace
{
	/*!
		@brief
			What a column shows from above: water where there is any, else its top material.
		@param maps
			The generated terrain maps
		@param i
			The column index
		@param water
			The material id to report for wet columns
		@return
			The material seen from above
		*/
	voxel::MaterialId shownMaterial(const voxel::TerrainMaps& maps, std::size_t i, voxel::MaterialId water)
	{
		return maps.waterTop_[i] >= 0 ? water : maps.topMaterial_[i];
	}

	/*!
		@brief
			Prints what the eye would judge.

			How much of the map each material covers, how rough the voxel surface is, and whether any
			column stands alone as a spike; also the height range and lake area.
		@param maps
			The generated terrain maps
		@param materials
			The material table, for names
		@param water
			The material id counted as lake
		@param voxelSize
			Edge of one voxel in metres, for the lake area
		*/
	void reportTerrain(const voxel::TerrainMaps& maps, const voxel::MaterialTable& materials,
					   voxel::MaterialId water, float voxelSize)
	{
		const auto [low, high] = std::minmax_element(maps.height_.h_.begin(), maps.height_.h_.end());
		std::cout << "[voxel-probe] height " << *low << " .. " << *high << " m\n";

		std::vector<std::size_t> cover(materials.size(), 0);
		for (std::size_t i = 0; i < maps.surface_.size(); ++i)
			++cover[shownMaterial(maps, i, water)];

		std::cout << "[voxel-probe] cover:";
		for (std::size_t m = 0; m < cover.size(); ++m)
		{
			if (cover[m] == 0)
				continue;
			const double share = 100.0 * static_cast<double>(cover[m]) / static_cast<double>(maps.surface_.size());
			std::cout << " " << materials.get(static_cast<voxel::MaterialId>(m)).name_ << " " << share << "%";
		}
		std::cout << "\n";

		const double lakeArea = static_cast<double>(cover[water]) * voxelSize * voxelSize;

		int maxStep = 0;
		std::size_t spikes = 0, bigSteps = 0;
		for (int z = 0; z < maps.depth_; ++z)
		for (int x = 0; x < maps.width_; ++x)
		{
			const int h = maps.surface_[maps.index(x, z)];
			const auto at = [&](int ax, int az)
			{
				return static_cast<int>(maps.surface_[maps.index(std::clamp(ax, 0, maps.width_ - 1), std::clamp(az, 0, maps.depth_ - 1))]);
			};

			const int n[4] = { at(x - 1, z), at(x + 1, z), at(x, z - 1), at(x, z + 1) };
			const int above = h - std::max({ n[0], n[1], n[2], n[3] });
			const int step = std::max({ std::abs(h - n[0]), std::abs(h - n[1]), std::abs(h - n[2]), std::abs(h - n[3]) });

			maxStep = std::max(maxStep, step);
			if (above >= 3) ++spikes;
			if (step >= 3) ++bigSteps;
		}

		std::cout << "[voxel-probe] lake area " << lakeArea << " m2, max neighbour step " << maxStep
			<< " voxels, columns with a step >= 3: " << bigSteps << ", spike columns: " << spikes << "\n";
	}

	/*!
		@brief
			FNV-1a hash of everything the fill reads.

			Unlike the .voxmap's own bytes it does not move when the material table grows, so it tells
			whether a change to the generator changed the terrain.
		@param maps
			The terrain maps to hash
		@return
			The 64-bit hash
		*/
	std::uint64_t contentHash(const voxel::TerrainMaps& maps)
	{
		std::uint64_t h = 0xCBF29CE484222325ull;
		const auto mix = [&h](const void* data, std::size_t bytes)
		{
			const auto* p = static_cast<const unsigned char*>(data);
			for (std::size_t i = 0; i < bytes; ++i)
			{
				h ^= p[i];
				h *= 0x100000001B3ull;
			}
		};

		mix(maps.surface_.data(), maps.surface_.size() * sizeof(std::int16_t));
		mix(maps.waterTop_.data(), maps.waterTop_.size() * sizeof(std::int16_t));
		mix(maps.topMaterial_.data(), maps.topMaterial_.size());
		mix(maps.topDepth_.data(), maps.topDepth_.size());
		mix(maps.soilDepth_.data(), maps.soilDepth_.size());
		mix(maps.biome_.data(), maps.biome_.size());
		mix(maps.strataShift_.data(), maps.strataShift_.size());
		return h;
	}

	/*!
		@brief
			Prints how much of the map each biome covers, plus the biome settings; nothing for non-biome maps.
		@param maps
			The generated terrain maps
		@param recipe
			The recipe, for biome names and settings
		*/
	void reportBiomes(const voxel::TerrainMaps& maps, const voxel::Recipe& recipe)
	{
		if (maps.biome_.empty())
			return;

		std::vector<std::size_t> columns(recipe.biomes_.entries_.size(), 0);
		for (const std::uint8_t b : maps.biome_)
			++columns[b];

		std::cout << "[voxel-probe] biomes (size " << recipe.biomes_.size_ << " m, blending " << recipe.biomes_.blending_
			<< ", datum " << recipe.biomes_.datumHeight_ << " m, relief x" << recipe.biomes_.reliefScale_ << "):";
		for (std::size_t b = 0; b < columns.size(); ++b)
			std::cout << " " << recipe.biomes_.entries_[b].name_ << " "
				<< 100.0 * static_cast<double>(columns[b]) / static_cast<double>(maps.biome_.size()) << "%";
		std::cout << "\n";
	}

	/*!
		@brief
			Writes an RGBA preview image as a PNG and logs where it went.

			Top-down picture: material colour under a north-west light. Good enough to judge the landform
			without opening a window.
		@param file
			The PNG path
		@param image
			The rendered preview
		*/
	void writeImage(const std::filesystem::path& file, const voxel::preview::Image& image)
	{
		if (image.width_ > 0 && stbi_write_png(file.string().c_str(), image.width_, image.height_, 4, image.rgba_.data(), image.width_ * 4) != 0)
			std::cout << "[voxel-probe] preview -> " << file.string() << "\n";
		else
			std::cerr << "[voxel-probe] could not write " << file.string() << "\n";
	}

	// Set by --stages: a picture of every pass, not only the finished terrain.
	bool g_writeStages = false;

	// Set by --brush: time the Editor's brush ops on the generated world.
	bool g_timeBrushes = false;

	/*!
		@brief
			Times one brush stamp per op and brush size on the main thread.

			Reading its region, the op, writing it back and re-mipping the LoD ancestors - everything
			voxel::System does inside its write window. Remeshing is not included; that runs on the workers
			afterwards.
		@param world
			The generated world; it is edited
		@param materials
			The material table
		@param maps
			The terrain maps, to find the ground in the middle of the map
		*/
	void timeBrushes(voxel::World& world, const voxel::MaterialTable& materials, const voxel::TerrainMaps& maps)
	{
		using namespace voxel::edit;
		using clock = std::chrono::steady_clock;

		// On the ground in the middle of the map.
		const int cx = maps.width_ / 2, cz = maps.depth_ / 2;
		const float ground = static_cast<float>(maps.surface_[maps.index(cx, cz)]) + 1.f;

		for (const float radius : { 10.f, 20.f, 60.f })
		for (const Op op : { Op::Add, Op::Subtract, Op::Grow, Op::Smooth, Op::Flatten, Op::Paint, Op::Erode })
		{
			Stamp stamp;
			stamp.centre_ = { static_cast<float>(cx) + 0.5f, ground, static_cast<float>(cz) + 0.5f };
			stamp.half_ = glm::vec3{ radius };

			OpParams params;
			params.op_ = op;
			params.material_ = materials.id("dirt");
			params.planeY_ = ground;
			params.seed_ = 1;

			const auto t0 = clock::now();
			VoxelRegion region;
			read(world, contextBox(stamp, params), region);
			const auto t1 = clock::now();
			const std::size_t changed = apply(region, stamp, params);
			const auto t2 = clock::now();
			const WriteResult written = write(world, region);
			world.remipAncestors(written.changed_, materials);
			const auto t3 = clock::now();

			const auto ms = [](clock::time_point a, clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
			std::cout << "[voxel-probe] brush " << info(op).name_ << " r=" << radius * world.dims().voxelSize_ << " m: read " << ms(t0, t1)
				<< " ms, op " << ms(t1, t2) << " ms, write+mip " << ms(t2, t3) << " ms, total " << ms(t0, t3) << " ms ("
				<< changed << " voxels, " << written.changed_.size() << " sections + " << written.neighbours_.size() << " neighbours to remesh)\n";
		}
	}

	/*!
		@brief
			Writes the pictures the Editor's pipeline panel shows, as files, and logs each stage.

			Each stage shaded, and what it changed from the stage before. The same renderer draws both, so
			this is how to judge the panel without a window.
		@param dir
			The output folder
		@param stages
			The captured generation stages, in order
		@param materials
			The material table
		@param recipe
			The recipe, for biome colours
		*/
	void writeStages(const std::filesystem::path& dir, const std::vector<voxel::preview::StageCapture>& stages,
					 const voxel::MaterialTable& materials, const voxel::Recipe& recipe)
	{
		const std::vector<glm::vec3> biomeColours = voxel::preview::biomeColours(recipe);

		for (std::size_t i = 0; i < stages.size(); ++i)
		{
			const voxel::preview::StageCapture& stage = stages[i];
			const voxel::preview::StageCapture* previous = i > 0 ? &stages[i - 1] : nullptr;
			const std::string stem = "stage_" + std::to_string(i) + "_"
				+ std::string(voxel::kGenStages[static_cast<std::size_t>(stage.stage_)].name_);

			std::cout << "[voxel-probe] stage " << voxel::kGenStages[static_cast<std::size_t>(stage.stage_)].name_
				<< ": " << stage.milliseconds_ << " ms, height " << stage.lowest_ << " .. " << stage.highest_
				<< " m, moved " << stage.movedVolume_ << " m3, under water " << stage.wetShare_ * 100.f << "%\n";

			for (const voxel::preview::Mode mode : { voxel::preview::Mode::Shaded, voxel::preview::Mode::Change,
													 voxel::preview::Mode::Water, voxel::preview::Mode::Biomes })
			{
				// Water and biomes once each, where they first appear and at the end.
				if (!voxel::preview::supports(stage, previous, mode))
					continue;
				if (mode == voxel::preview::Mode::Water && stage.stage_ != voxel::GenStage::Lakes)
					continue;
				if (mode == voxel::preview::Mode::Biomes && stage.stage_ != voxel::GenStage::Materials)
					continue;

				voxel::preview::RenderOptions options;
				options.mode_ = mode;
				options.biomeColours_ = biomeColours;
				writeImage(dir / (stem + "_" + std::string(voxel::preview::modeName(mode)) + ".png"),
						   voxel::preview::render(stage, previous, materials, options));
			}
		}
	}

	/*!
		@brief
			Generates, mips, meshes and writes a bounded map, then prints timings, stats and the content hash.

			Also writes preview.png (and the stage pictures with --stages) and times brushes with --brush.
		@param resources
			Resource manager over the asset root
		@param recipePath
			The recipe path; the .voxmap is written beside it
		@param composed
			An already composed recipe to use instead of loading recipePath, or nullptr
		*/
	void runProbe(vox::ResourceManager& resources, const std::string& recipePath, const voxel::Recipe* composed)
	{
		using clock = std::chrono::steady_clock;
		const auto ms = [](clock::time_point a, clock::time_point b)
		{
			return std::chrono::duration<double, std::milli>(b - a).count();
		};

		const vox::ResourceHandle materialsFile = resources.load("voxel/materials.json");

		const voxel::MaterialTable materials = voxel::MaterialTable::parse(materialsFile->bytes);
		const voxel::Recipe recipe = composed ? *composed : voxel::Recipe::parse(resources.load(recipePath)->bytes);
		const voxel::TerrainGenerator generator(recipe, materials);

		util::thread::JobQueue jobs;
		voxel::World world(generator.dims());

		const voxel::MaterialId waterId = materials.id(recipe.water_.material_);

		// Always watched: the content hash below is then also the check that
		// an observer changes nothing. Full size unless every stage is kept.
		voxel::preview::CapturingObserver observer(waterId == voxel::kUnknownMaterial ? voxel::kAir : waterId, g_writeStages ? 512 : 1 << 20);

		const auto t0 = clock::now();
		std::atomic<int> progress{ 0 };
		const std::optional<voxel::TerrainMaps> maps = generator.buildMaps(&jobs, &progress, nullptr, &observer);
		if (!maps)
			throw std::runtime_error("generation was cancelled");
		const auto tMaps = clock::now();

		generator.fill(world, *maps, &jobs, &progress, nullptr);
		const auto t1 = clock::now();

		world.rebuildLevels(materials, &jobs);
		const auto t2 = clock::now();

		// Mesh every level, counting quads.
		std::atomic<std::size_t> quads{ 0 }, meshed{ 0 };
		for (int level = 0; level < voxel::kLodLevels; ++level)
		{
			const glm::ivec3 d = world.sectionsAt(level);
			for (int sy = 0; sy < d.y; ++sy)
			for (int sz = 0; sz < d.z; ++sz)
			{
				jobs.push([&, level, sy, sz, d]
				{
					for (int sx = 0; sx < d.x; ++sx)
					{
						const voxel::MeshInput in = voxel::gatherMeshInput(world, level, { sx, sy, sz });
						if (in.self_->isAir())
							continue;
						const voxel::SectionMesh m = voxel::meshSection(in, materials, false);
						quads.fetch_add(m.quadCount(), std::memory_order_relaxed);
						meshed.fetch_add(1, std::memory_order_relaxed);
					}
				});
			}
		}
		jobs.waitIdle();
		const auto t3 = clock::now();

		voxel::voxmap::Info info;
		info.seed_ = recipe.seed_;
		info.recipePath_ = recipePath;
		std::filesystem::path out = resources.resolve(recipePath);
		out.replace_extension(".voxmap");
		voxel::voxmap::writeFile(out, world, materials, info);
		const auto t4 = clock::now();

		std::cout << "[voxel-probe] " << world.dims().sections_.x << "x" << world.dims().sections_.y << "x" << world.dims().sections_.z
			<< " sections, level 0 dense " << world.denseCount(0) << " / " << world.sectionCount(0)
			<< ", level 1 dense " << world.denseCount(1)
			<< ", level 2 dense " << world.denseCount(2)
			<< ", level 3 dense " << world.denseCount(3) << "\n";
		std::cout << "[voxel-probe] prepass " << ms(t0, tMaps) << " ms, fill " << ms(tMaps, t1) << " ms on " << jobs.workerCount()
			<< " workers (" << progress.load() << " / " << generator.workUnits() << " units), mip " << ms(t1, t2)
			<< " ms, mesh " << meshed.load() << " sections / " << quads.load() << " quads in " << ms(t2, t3)
			<< " ms, write " << ms(t3, t4) << " ms -> " << out.string()
			<< " (" << std::filesystem::file_size(out) / 1024 << " KB)\n";

		const voxel::MaterialId water = materials.id(recipe.water_.material_);
		reportTerrain(*maps, materials, water == voxel::kUnknownMaterial ? voxel::kAir : water, recipe.voxelSize_);
		reportBiomes(*maps, recipe);
		std::cout << "[voxel-probe] content hash " << std::hex << contentHash(*maps) << std::dec << "\n";

		// Beside the build output when there is one: inside Assets it
		// would be cooked as a texture.
		std::error_code ec;
		std::filesystem::path previewDir = resources.root().parent_path() / "build";
		if (!std::filesystem::is_directory(previewDir, ec))
			previewDir = std::filesystem::temp_directory_path();
		// A folder of its own: a --stages run writes a dozen pictures.
		previewDir /= "voxel_probe";
		std::filesystem::create_directories(previewDir, ec);
		// The finished terrain is the last stage, shaded: material colour
		// under a north-west light.
		if (!observer.stages().empty())
		{
			const std::vector<voxel::preview::StageCapture>& stages = observer.stages();
			writeImage(previewDir / "preview.png",
					   voxel::preview::render(stages.back(), nullptr, materials, voxel::preview::RenderOptions{}));
			if (g_writeStages)
				writeStages(previewDir, stages, materials, recipe);
		}

		// Last: it edits the world, which has been written and measured by now.
		if (g_timeBrushes)
			timeBrushes(world, materials, *maps);
	}
}

namespace
{
	/*!
		@brief
			Splits a comma-separated list, dropping empty items.
		@param list
			The text to split
		@return
			The items in order
		*/
	std::vector<std::string> splitCommas(const std::string& list)
	{
		std::vector<std::string> out;
		std::size_t from = 0;
		while (from <= list.size())
		{
			const std::size_t comma = std::min(list.find(',', from), list.size());
			if (comma > from)
				out.push_back(list.substr(from, comma - from));
			from = comma + 1;
		}
		return out;
	}

	/*!
		@brief
			Measures the time elapsed since a point.
		@param from
			The start time
		@return
			Milliseconds since from
		*/
	double msSince(std::chrono::steady_clock::time_point from)
	{
		return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - from).count();
	}

	/*!
		@brief
			--endless: builds a square of tiles from the middle of a streamed terrain and checks the seams.

			Builds them the way the streamer would, one at a time on this thread, and says what it cost and
			whether the tiles meet; also times cell builds. Writes the tiles' cores side by side, hillshaded,
			to build/voxel_probe/endless.png - a seam would show there as a line.
		@param resources
			Resource manager over the asset root
		@param recipe
			The endless recipe
		@param tilesAcross
			Tiles per side of the square, clamped to the world
		*/
	void runEndless(vox::ResourceManager& resources, const voxel::Recipe& recipe, int tilesAcross)
	{
		using namespace voxel;

		const MaterialTable materials = MaterialTable::parse(resources.load("voxel/materials.json")->bytes);
		const TileGenerator generator(recipe, materials);

		const int core = generator.coreVoxels(), pad = generator.padVoxels();
		const int tilesInWorld = recipe.size_.x / core;
		tilesAcross = std::clamp(tilesAcross, 1, tilesInWorld);
		const int first = (tilesInWorld - tilesAcross) / 2;

		std::cout << "[voxel-probe] endless: world " << recipe.size_.x << " x " << recipe.size_.y << " x " << recipe.size_.z
			<< " voxels, tile " << core << " + 2 x " << pad << " pad, identical band " << generator.pureVoxels()
			<< " (the rules reach " << generator.reachVoxels() << "), " << recipe.erosion_.droplets_ << " droplets a tile\n";

		// ---------------------------- Tiles ----------------------------

		std::vector<std::shared_ptr<const Tile>> tiles;
		double tileMs = 0;
		for (int tz = 0; tz < tilesAcross; ++tz)
		for (int tx = 0; tx < tilesAcross; ++tx)
		{
			const auto started = std::chrono::steady_clock::now();
			tiles.push_back(generator.buildTile({ first + tx, first + tz }));
			const double ms = msSince(started);
			tileMs += ms;

			std::cout << "[voxel-probe]   tile (" << first + tx << ", " << first + tz << ") " << ms << " ms, "
				<< tiles.back()->bytes() / 1024 << " KB, hash " << std::hex << contentHash(tiles.back()->maps_) << std::dec << "\n";
		}
		std::cout << "[voxel-probe] tiles: " << tileMs / static_cast<double>(tiles.size()) << " ms each on one thread\n";

		// A tile made again is the same tile.
		{
			const auto again = generator.buildTile(tiles.front()->tile_);
			std::cout << "[voxel-probe] rebuilt tile " << (contentHash(again->maps_) == contentHash(tiles.front()->maps_) ? "matches" : "DIFFERS") << "\n";
		}

		// ---------------------------- Seams ----------------------------
		//
		// The same world column, computed by the tile it belongs to and by
		// its neighbour's pad.
		const int margin = generator.reachVoxels();
		const int span = core + 2 * pad;
		std::size_t compared = 0, differing = 0;
		int worstStep = 0;

		const auto at = [&](int tx, int tz) -> const TerrainMaps& { return tiles[static_cast<std::size_t>(tz * tilesAcross + tx)]->maps_; };
		const auto seam = [&](const TerrainMaps& left, const TerrainMaps& right, bool alongX)
		{
			for (int along = margin; along < span - margin; ++along)
			{
				for (int across = -(pad - margin); across < pad - margin; ++across)
				{
					const int inLeft = pad + core + across, inRight = pad + across;
					const std::size_t i = alongX ? left.index(inLeft, along) : left.index(along, inLeft);
					const std::size_t j = alongX ? right.index(inRight, along) : right.index(along, inRight);

					++compared;
					if (left.surface_[i] != right.surface_[j] || left.topMaterial_[i] != right.topMaterial_[j]
						|| left.waterTop_[i] != right.waterTop_[j] || left.soilDepth_[i] != right.soilDepth_[j])
						++differing;
				}

				// The step a player would walk over: last column of one core, first of the next.
				const std::size_t a = alongX ? left.index(pad + core - 1, along) : left.index(along, pad + core - 1);
				const std::size_t b = alongX ? right.index(pad, along) : right.index(along, pad);
				worstStep = std::max(worstStep, std::abs(left.surface_[a] - right.surface_[b]));
			}
		};

		for (int tz = 0; tz < tilesAcross; ++tz)
		for (int tx = 0; tx < tilesAcross; ++tx)
		{
			if (tx + 1 < tilesAcross) seam(at(tx, tz), at(tx + 1, tz), true);
			if (tz + 1 < tilesAcross) seam(at(tx, tz), at(tx, tz + 1), false);
		}

		// For scale: the largest step between neighbouring columns inside a tile.
		int insideStep = 0;
		{
			const TerrainMaps& m = tiles.front()->maps_;
			for (int z = pad; z < pad + core; ++z)
			for (int x = pad; x + 1 < pad + core; ++x)
				insideStep = std::max(insideStep, std::abs(m.surface_[m.index(x, z)] - m.surface_[m.index(x + 1, z)]));
		}

		std::cout << "[voxel-probe] seams: " << differing << " of " << compared << " shared columns differ; largest step across a seam "
			<< worstStep << " voxels (inside a tile: " << insideStep << ")\n";

		// ---------------------------- Cells ----------------------------

		const Tile& tile = *tiles.front();
		const glm::ivec2 firstCell = tile.tile_ * generator.tileCells();
		double fineMs = 0, coarseMs = 0;
		std::size_t denseFine = 0, denseCoarse = 0;
		int cells = 0;

		for (int cz = 0; cz < generator.tileCells(); ++cz)
		for (int cx = 0; cx < generator.tileCells(); ++cx)
		{
			auto started = std::chrono::steady_clock::now();
			const CellSections fine = generator.buildCell(tile, firstCell + glm::ivec2{ cx, cz }, 0);
			fineMs += msSince(started);

			started = std::chrono::steady_clock::now();
			const CellSections coarse = generator.buildCell(tile, firstCell + glm::ivec2{ cx, cz }, kLodLevels - 1);
			coarseMs += msSince(started);

			for (const auto& level : fine.sections_)
				for (const Section& s : level)
					denseFine += s.isUniform() ? 0 : 1;
			for (const Section& s : coarse.sections_[kLodLevels - 1])
				denseCoarse += s.isUniform() ? 0 : 1;
			++cells;
		}

		std::cout << "[voxel-probe] cells: detail 0 " << fineMs / cells << " ms and " << denseFine / static_cast<std::size_t>(cells)
			<< " dense sections (" << denseFine / static_cast<std::size_t>(cells) * 32 << " KB) each; detail " << kLodLevels - 1 << " "
			<< coarseMs / cells << " ms and " << denseCoarse / static_cast<std::size_t>(cells) << " dense\n";

		const util::memory::BlockPool::Stats pool = sectionPool().stats();
		std::cout << "[voxel-probe] section pool: peak " << pool.blocksPeak_ * pool.blockBytes_ / 1048576 << " MB, "
			<< pool.allocations_ << " blocks from " << pool.slabsCreated_ << " slabs\n";

		// --------------------------- Picture ---------------------------

		const int step = std::max(1, tilesAcross * core / 1536);
		const int side = tilesAcross * core / step;

		preview::Image image;
		image.width_ = side;
		image.height_ = side;
		image.rgba_.resize(static_cast<std::size_t>(side) * side * 4);

		const auto surfaceAt = [&](int wx, int wz)
		{
			wx = std::clamp(wx, 0, tilesAcross * core - 1);
			wz = std::clamp(wz, 0, tilesAcross * core - 1);
			const TerrainMaps& m = at(wx / core, wz / core);
			return m.index(pad + wx % core, pad + wz % core);
		};

		for (int pz = 0; pz < side; ++pz)
		for (int px = 0; px < side; ++px)
		{
			const int wx = px * step, wz = pz * step;
			const TerrainMaps& m = at(wx / core, wz / core);
			const std::size_t i = surfaceAt(wx, wz);

			const bool wet = m.waterTop_[i] >= 0;
			const float h = static_cast<float>(wet ? m.waterTop_[i] : m.surface_[i]);
			const auto heightOf = [&](int x, int z)
			{
				const TerrainMaps& n = at(std::clamp(x, 0, tilesAcross * core - 1) / core, std::clamp(z, 0, tilesAcross * core - 1) / core);
				const std::size_t k = surfaceAt(x, z);
				return static_cast<float>(n.waterTop_[k] >= 0 ? n.waterTop_[k] : n.surface_[k]);
			};

			// Lit from the north-west.
			const float dx = heightOf(wx + step, wz) - heightOf(wx - step, wz);
			const float dz = heightOf(wx, wz + step) - heightOf(wx, wz - step);
			const float shade = std::clamp(0.75f - 0.35f * (dx + dz) / static_cast<float>(step), 0.25f, 1.15f);
			(void)h;

			const glm::vec3 linear = materials.get(wet ? materials.id(recipe.water_.material_) : m.topMaterial_[i]).colourLinear_;
			const std::size_t o = (static_cast<std::size_t>(pz) * side + px) * 4;
			for (int c = 0; c < 3; ++c)
				image.rgba_[o + c] = static_cast<std::uint8_t>(std::clamp(std::pow(linear[c], 1.f / 2.2f) * shade, 0.f, 1.f) * 255.f);
			image.rgba_[o + 3] = 255;
		}

		const std::filesystem::path dir = std::filesystem::path("build") / "voxel_probe";
		std::filesystem::create_directories(dir);
		writeImage(dir / "endless.png", image);
	}

	/*!
		@brief
			--biomes and its companions: composes the recipe, writes it to --out, then runs the probe on it.

			Runs runEndless for an endless recipe, runProbe otherwise. Throws on an unknown option or a
			missing value.
		@param resources
			Resource manager over the asset root
		@param argc
			Number of command-line arguments
		@param argv
			The command-line arguments; options start at index 2
		*/
	void runComposed(vox::ResourceManager& resources, int argc, char* argv[])
	{
		voxel::GenerateSettings settings;
		std::string out = "maps/biomes_demo.voxgen";
		int endlessTiles = 0;

		for (int a = 2; a < argc; ++a)
		{
			const std::string option = argv[a];
			const auto need = [&](int count)
			{
				if (a + count >= argc)
					throw std::runtime_error(option + " needs " + std::to_string(count) + " value(s)");
			};

			if (option == "--biomes")			{ need(1); settings.biomes_ = splitCommas(argv[++a]); }
			else if (option == "--size")		{ need(3); settings.size_ = { std::stoi(argv[a + 1]), std::stoi(argv[a + 2]), std::stoi(argv[a + 3]) }; a += 3; }
			else if (option == "--origin")		{ need(3); settings.origin_ = { std::stof(argv[a + 1]), std::stof(argv[a + 2]), std::stof(argv[a + 3]) }; a += 3; }
			else if (option == "--seed")		{ need(1); settings.seed_ = std::stoull(argv[++a]); }
			else if (option == "--blending")	{ need(1); settings.blending_ = std::stof(argv[++a]); }
			else if (option == "--biome-size")	{ need(1); settings.biomeSize_ = std::stof(argv[++a]); }
			else if (option == "--out")			{ need(1); out = argv[++a]; }
			else if (option == "--endless")		{ settings.endless_ = true; endlessTiles = std::max(endlessTiles, 2); }
			else if (option == "--tiles")		{ need(1); endlessTiles = std::stoi(argv[++a]); }
			else if (option == "--tile-cells")	{ need(1); settings.tileCells_ = std::stoi(argv[++a]); }
			else
				throw std::runtime_error("unknown option " + option);
		}

		const voxel::BiomeLibrary library = voxel::BiomeLibrary::parse(resources.load("voxel/biomes.json")->bytes);
		const voxel::Recipe recipe = voxel::composeBiomeRecipe(library, settings);

		const std::filesystem::path file = resources.resolve(out);
		std::ofstream(file, std::ios::binary | std::ios::trunc) << recipe.toJson();
		std::cout << "[voxel-probe] recipe -> " << file.string() << "\n";

		if (settings.endless_)
			runEndless(resources, recipe, endlessTiles);
		else
			runProbe(resources, out, &recipe);
	}
}

/*!
	@brief
		Strips --stages / --brush, then runs the composed (--biomes) or recipe form of the probe.
	@param argc
		Number of command-line arguments
	@param argv
		The command-line arguments
	@return
		0 on success, 1 on failure, 2 on a usage error
	*/
int main(int argc, char* argv[])
{
	if (argc < 2)
	{
		std::cerr << "usage: VoxelProbe <assetRoot> [recipe, default maps/arena01.voxgen]\n"
					 "       VoxelProbe <assetRoot> --biomes a,b[,..] [--size X Y Z] [--origin X Y Z] [--seed N]\n"
					 "                  [--blending 0..1] [--biome-size METRES] [--out maps/biomes_demo.voxgen]\n"
					 "                  [--endless [--tiles N] [--tile-cells 1|2|4]]  a streamed terrain: --size is its whole\n"
					 "                  extent, and N x N tiles from its middle are built, timed and checked at their seams\n"
					 "       --stages anywhere also writes a picture of every generation pass\n"
					 "       --brush anywhere also times the Editor's brush ops on the generated world\n";
		return 2;
	}

	// --stages and --brush go with either form, so they are taken out before the forms parse.
	for (int a = 2; a < argc; ++a)
	{
		const std::string option = argv[a];
		if (option != "--stages" && option != "--brush")
			continue;
		(option == "--stages" ? g_writeStages : g_timeBrushes) = true;
		for (int b = a; b + 1 < argc; ++b)
			argv[b] = argv[b + 1];
		--argc;
		--a;
	}

	try
	{
		vox::ResourceManager resources{ argv[1] };
		if (argc >= 3 && std::string(argv[2]).rfind("--", 0) == 0)
			runComposed(resources, argc, argv);
		else
			runProbe(resources, argc >= 3 ? argv[2] : "maps/arena01.voxgen", nullptr);
		return 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << "[voxel-probe] failed: " << e.what() << "\n";
		return 1;
	}
}
