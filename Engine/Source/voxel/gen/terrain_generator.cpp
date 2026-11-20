/*************************************************************************************************************
\file       terrain_generator.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Implements TerrainGenerator: the prepass (landform, erosion, lakes, upsample and detail,
            surface rules), the tile fade and lake confinement for streamed tiles, and the voxel fill.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "terrain_generator.hpp"

// Voxel
#include "biome/biome_terrain.hpp"
#include "erosion.hpp"
#include "hydrology.hpp"
#include "parallel_rows.hpp"
#include "terrain_shape.hpp"

// Noise
#include "../../util/noise/fbm.hpp"
#include "../../util/noise/hash.hpp"

// GLM
#include <glm/glm.hpp>

// Profiling
#include "../../profile/profiler.hpp"

// stds
#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <vector>


namespace voxel
{
	namespace
	{
		constexpr int S = kSectionSize;

		// Prepass stages other than the droplets, one progress tick each:
		// shape, thermal, lakes, upsample + detail, surface rules.
		constexpr int kStageTicks = 5;

		// Detail octaves finer than this many voxels are dropped.
		constexpr double kMinDetailVoxels = 4.0;

		// A column may stand this many voxels above all its neighbours
		// before clampSpikes pulls it down.
		constexpr float kMaxSpikeVoxels = 2.f;

		// How far apart (metres) a biome's rock bands rise and fall.
		constexpr double kStrataWavelength = 30.0;

		// A lake of a tile keeps this many voxels, on top of the shore's reach,
		// clear of the band where the ground is faded: sand and shallows are
		// laid out around a lake, and they must not reach the seam either.
		constexpr int kLakeMarginVoxels = 4;
	}

	/*!
		@brief
			A generator for one tile of a streamed terrain; validates the tile's bands against its recipe.
		@param recipe
			The tile's window; must be biome terrain
		@param materials
			Material names in the recipe are resolved here
		@param tile
			Core, pad, pure band, blend and erosion salt of the tile
		*/
	TerrainGenerator::TerrainGenerator(const Recipe& recipe, const MaterialTable& materials, const TileOptions& tile)
		: TerrainGenerator(recipe, materials)
	{
		if (!biomes_)
			throw std::runtime_error("TerrainGenerator: only biome terrain can be made in tiles");

		const int cellVoxels = recipe_.erosion_.cellVoxels_;
		if (tile.core_ <= 0 || tile.pad_ < 0 || tile.pure_ < 0 || tile.blend_ < 0
			|| tile.core_ % cellVoxels != 0 || tile.pad_ % cellVoxels != 0)
			throw std::runtime_error("TerrainGenerator: a tile's core and pad must be whole erosion cells");

		if (recipe_.size_.x != tile.core_ + 2 * tile.pad_ || recipe_.size_.z != recipe_.size_.x)
			throw std::runtime_error("TerrainGenerator: a tile's recipe must be its core plus the pad on every side");

		if (2 * (tile.pure_ + tile.blend_) > tile.core_)
			throw std::runtime_error("TerrainGenerator: a tile's pure and blend bands leave none of it eroded");

		tile_ = tile;
	}

	/*!
		@brief
			Resolves the recipe's materials, derives the seeds and, for biome terrain, the biome covers.
		@param recipe
			The terrain to make; an endless recipe throws
		@param materials
			Material names in the recipe are resolved here; throws when one is missing or air
		*/
	TerrainGenerator::TerrainGenerator(const Recipe& recipe, const MaterialTable& materials)
		: recipe_(recipe)
	{
		// The whole-map passes below would try to erode kilometres at once.
		if (recipe_.stream_.endless_)
			throw std::runtime_error("an endless terrain is generated tile by tile (voxel::TileGenerator), never as one map");

		const auto resolve = [&materials](const std::string& name, const char* role)
		{
			const MaterialId id = materials.id(name);
			if (id == kUnknownMaterial || id == kAir)
				throw std::runtime_error(std::string(".voxgen: ") + role + " names unknown or non-solid material \"" + name + "\"");
			return id;
		};

		surface_.top_ = resolve(recipe_.surface_.top_, "surface.top");
		surface_.under_ = resolve(recipe_.surface_.under_, "surface.under");
		surface_.base_ = resolve(recipe_.surface_.base_, "surface.base");
		surface_.sand_ = resolve(recipe_.surface_.sand_, "surface.sand");
		surface_.snow_ = resolve(recipe_.surface_.snow_, "surface.snow");
		if (recipe_.water_.enabled_)
			water_ = resolve(recipe_.water_.material_, "water.material");

		using util::noise::hashString;
		seedErosion_ = recipe_.seed_ ^ hashString("erosion");
		seedDetail_ = recipe_.seed_ ^ hashString("detailNoise");
		seedCaves_ = recipe_.seed_ ^ hashString("caves");
		seedStrata_ = recipe_.seed_ ^ hashString("biomes.strata");

		if (!recipe_.biomes_.enabled_)
			return;

		biomes_ = std::make_shared<const BiomeTerrain>(recipe_);
		shellVoxels_ = std::max(0, static_cast<int>(std::lround(recipe_.biomes_.shellDepth_ / recipe_.voxelSize_)));

		for (const BiomeDefinition& biome : recipe_.biomes_.entries_)
		{
			const std::string where = "biome \"" + biome.name_ + "\" ";

			BiomeCover cover;
			cover.rules_ = biome.rules_;
			cover.rules_.snowline_ *= static_cast<float>(landformScale(biome, recipe_.biomes_.size_));	// a shrunk range keeps its snow cap
			cover.materials_.top_ = resolve(biome.palette_.top_, (where + "palette.top").c_str());
			cover.materials_.under_ = resolve(biome.palette_.under_, (where + "palette.under").c_str());
			cover.materials_.base_ = resolve(biome.palette_.rock_, (where + "palette.rock").c_str());
			cover.materials_.sand_ = resolve(biome.palette_.sand_, (where + "palette.sand").c_str());
			cover.materials_.snow_ = resolve(biome.palette_.snow_, (where + "palette.snow").c_str());

			for (const BiomeDefinition::Band& band : biome.strata_)
			{
				BiomeCover::Band resolved;
				resolved.material_ = resolve(band.material_, (where + "strata").c_str());
				resolved.voxels_ = std::max(1, static_cast<int>(std::lround(band.thickness_ / recipe_.voxelSize_)));
				cover.strataVoxels_ += resolved.voxels_;
				cover.strata_.push_back(resolved);
			}

			cover.ownRock_ = !cover.strata_.empty() || cover.materials_.base_ != surface_.base_;
			covers_.push_back(std::move(cover));
		}
	}

	/*!
		@brief
			Dimensions a world for this recipe must have.
		@return
			The world dimensions from the recipe's size, voxel size and origin
		*/
	WorldDims TerrainGenerator::dims() const
	{
		return WorldDims::fromVoxels(recipe_.size_, recipe_.voxelSize_, recipe_.origin_);
	}

	/*!
		@brief
			Total progress increments of generate(): droplet ticks, stage ticks and one per section column.
		@return
			The number of increments
		*/
	int TerrainGenerator::workUnits() const
	{
		const glm::ivec3 sections = dims().sections_;
		return hydraulicTicks(recipe_.erosion_) + kStageTicks + sections.x * sections.z;
	}

	/*!
		@brief
			Runs the prepass: landform, hydraulic and thermal erosion, lakes, upsample with detail, surface rules.
		@param jobs
			Queue for the parallel passes, or null
		@param progress
			Incremented as passes complete, if given
		@param cancel
			Polled between passes
		@param observer
			Shown the terrain after each stage, if given
		@return
			The terrain maps, or empty if cancelled
		*/
	std::optional<TerrainMaps> TerrainGenerator::buildMaps(util::thread::JobQueue* jobs,
														   std::atomic<int>* progress, const std::atomic<bool>* cancel,
														   GenStageObserver* observer) const
	{
		VOX_PROFILE_FUNCTION_C(profile::colour::kVoxel);

		const Recipe& r = recipe_;
		const auto cancelled = [cancel] { return cancel && cancel->load(std::memory_order_relaxed); };
		const auto tick = [progress] { if (progress) progress->fetch_add(1, std::memory_order_relaxed); };

		// Shows the observer, if there is one, what a stage left behind. The
		// clock restarts afterwards, so a stage is not charged for the time
		// the observer took to copy the one before it.
		auto stageStarted = std::chrono::steady_clock::now();
		const auto show = [&](GenStage stage, const HeightField* height, const HeightField* sediment,
							  const HeightField* water, const TerrainMaps* maps)
		{
			if (!observer)
				return;

			GenStageView view;
			view.stage_ = stage;
			view.height_ = height;
			view.sediment_ = sediment;
			view.water_ = water;
			view.maps_ = maps;
			view.milliseconds_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - stageStarted).count();
			observer->onStage(view);
			stageStarted = std::chrono::steady_clock::now();
		};

		// Erosion runs on a grid a few voxels coarse: gullies and scree are
		// decimetres to metres wide, and anything finer only adds noise.
		const int cellVoxels = r.erosion_.cellVoxels_;
		const float cell = static_cast<float>(cellVoxels) * r.voxelSize_;
		const int coarseWidth = (r.size_.x + cellVoxels - 1) / cellVoxels + 1;
		const int coarseDepth = (r.size_.z + cellVoxels - 1) / cellVoxels + 1;

		// Biome terrain also says, cell by cell, how readily the ground washes
		// away and how steep its rock stands.
		std::optional<BiomeFields> fields;
		if (biomes_)
			fields = biomes_->build(coarseWidth, coarseDepth, jobs);

		HeightField coarse = fields ? std::move(fields->height_) : TerrainShape(r, cell).build(coarseWidth, coarseDepth, jobs);
		tick();
		if (cancelled())
			return std::nullopt;
		show(GenStage::Base, &coarse, nullptr, nullptr, nullptr);

		// A tile keeps the ground as the biomes gave it: what erosion did is
		// faded back to this towards the tile's border.
		HeightField base;
		if (tile_)
			base = coarse;

		HeightField sediment(coarseWidth, coarseDepth, cell, 0.f);
		const std::uint64_t seedDroplets = seedErosion_ ^ (tile_ ? tile_->erosionSalt_ : 0);
		if (!erodeHydraulic(coarse, sediment, r.erosion_, seedDroplets, progress, cancel, fields ? &fields->erodibility_ : nullptr))
			return std::nullopt;
		show(GenStage::Hydraulic, &coarse, &sediment, nullptr, nullptr);

		if (!erodeThermal(coarse, sediment, r.erosion_, cancel, fields ? &fields->talus_ : nullptr))
			return std::nullopt;

		if (tile_)
			blendTowardsBase(coarse, sediment, base);

		tick();
		show(GenStage::Thermal, &coarse, &sediment, nullptr, nullptr);

		// The lake pass levels small pits in place. In the faded band that
		// would move ground the neighbouring tile computes without it.
		HeightField beforeLakes;
		if (tile_)
			beforeLakes = coarse;

		HeightField lakes = findLakes(coarse, r.water_);

		if (tile_)
			confineLakes(coarse, lakes, beforeLakes);

		tick();
		if (cancelled())
			return std::nullopt;
		show(GenStage::Lakes, &coarse, &sediment, &lakes, nullptr);

		// One sample per voxel column, taken at the column's centre.
		const int width = r.size_.x, depth = r.size_.z;
		const float scale = 1.f / static_cast<float>(cellVoxels), offset = 0.5f * scale;
		const HeightField smooth = resampleSmooth(coarse, width, depth, r.voxelSize_, scale, offset);
		const HeightField fineSediment = resampleLinear(sediment, width, depth, r.voxelSize_, scale, offset);

		// Slope-gated detail: roughness where the ground is steep, none
		// where it is flat, so plains and lake beds stay smooth.
		HeightField height = smooth;
		if (r.detailNoise_.layer_.amplitude_ > 0.0)
		{
			const util::noise::FbmParams fbm = r.detailNoise_.layer_.fbm(kMinDetailVoxels * r.voxelSize_);
			const float fullTan = std::tan(glm::radians(r.detailNoise_.fullSlopeDeg_));
			const float amplitude = static_cast<float>(r.detailNoise_.layer_.amplitude_);

			forRows(jobs, depth, [&](int z)
			{
				for (int x = 0; x < width; ++x)
				{
					const float g = std::min(1.f, slopeTangent(smooth, x, z) / fullTan);
					const float gate = g * g * std::sqrt(g);
					if (gate <= 0.f)
						continue;

					// Biome terrain is laid out in world space, and so is its grain.
					const double px = biomes_ ? biomes_->worldX(x) : static_cast<double>(x) * r.voxelSize_;
					const double pz = biomes_ ? biomes_->worldZ(z) : static_cast<double>(z) * r.voxelSize_;
					const double n = util::noise::fbm2(seedDetail_, px, pz, fbm);
					height.at(x, z) += amplitude * gate * static_cast<float>(n);
				}
			});
		}

		clampSpikes(height, kMaxSpikeVoxels * r.voxelSize_);

		// A lake keeps one level, so the fine grid only needs to know which
		// lake is near: the highest level among the coarse cells around it.
		HeightField water(width, depth, r.voxelSize_, kNoWater);
		forRows(jobs, depth, [&](int z)
		{
			const int cz = static_cast<int>((static_cast<float>(z) + 0.5f) * scale);
			for (int x = 0; x < width; ++x)
			{
				const int cx = static_cast<int>((static_cast<float>(x) + 0.5f) * scale);

				float level = kNoWater;
				for (int dz = -1; dz <= 1; ++dz)
				for (int dx = -1; dx <= 1; ++dx)
					level = std::max(level, lakes.clamped(cx + dx, cz + dz));

				if (height.at(x, z) < level)
					water.at(x, z) = level;
			}
		});
		tick();
		if (cancelled())
			return std::nullopt;
		show(GenStage::Detail, &height, &fineSediment, &water, nullptr);

		// Which biome each column shows, and where its rock bands sit.
		BiomeColumns columns;
		if (biomes_)
		{
			columns.covers_ = covers_;
			columns.datum_ = r.biomes_.datumHeight_;
			columns.reliefScale_ = r.biomes_.reliefScale_;
			columns.biome_.resize(height.size());
			columns.strataShift_.assign(height.size(), 0);

			util::noise::FbmParams wobble;
			wobble.frequency_ = 1.0 / kStrataWavelength;
			wobble.octaves_ = 2;

			forRows(jobs, depth, [&](int z)
			{
				const double pz = biomes_->worldZ(static_cast<double>(z) + 0.5);
				for (int x = 0; x < width; ++x)
				{
					const double px = biomes_->worldX(static_cast<double>(x) + 0.5);
					const std::size_t i = height.index(x, z);
					const int biome = biomes_->coverBiome(px, pz);
					columns.biome_[i] = static_cast<std::uint8_t>(biome);

					if (covers_[static_cast<std::size_t>(biome)].strata_.empty())
						continue;

					const double rise = r.biomes_.entries_[static_cast<std::size_t>(biome)].strataWobble_
						* util::noise::fbm2(seedStrata_, px, pz, wobble);
					columns.strataShift_[i] = static_cast<std::int8_t>(std::clamp(std::lround(rise / r.voxelSize_), -127l, 127l));
				}
			});
		}

		TerrainMaps maps = classifySurface(std::move(height), fineSediment, water, r, surface_, jobs, biomes_ ? &columns : nullptr);
		tick();
		show(GenStage::Materials, &maps.height_, &fineSediment, &water, &maps);
		return maps;
	}

	/*!
		@brief
			Erosion weight of a coarse cell: 0 in the pad and pure band, smoothstep across the blend, then 1.
		@param cx
			Coarse cell along x
		@param cz
			Coarse cell along z
		@return
			The weight in [0, 1]
		*/
	float TerrainGenerator::tileWeight(int cx, int cz) const
	{
		const TileOptions& t = *tile_;
		const int vx = cx * recipe_.erosion_.cellVoxels_, vz = cz * recipe_.erosion_.cellVoxels_;

		// Voxels inside the core's border; negative out in the pad.
		const int depth = std::min({ vx - t.pad_, t.pad_ + t.core_ - vx, vz - t.pad_, t.pad_ + t.core_ - vz });
		if (depth <= t.pure_)
			return 0.f;
		if (t.blend_ <= 0 || depth >= t.pure_ + t.blend_)
			return 1.f;

		const float s = static_cast<float>(depth - t.pure_) / static_cast<float>(t.blend_);
		return s * s * (3.f - 2.f * s);
	}

	/*!
		@brief
			Fades a tile's eroded ground back to the uneroded base towards its border.
		@param coarse
			Eroded heights, blended in place; exactly the base where the weight is 0
		@param sediment
			Net sediment, scaled by the same weight
		@param base
			The uneroded ground
		*/
	void TerrainGenerator::blendTowardsBase(HeightField& coarse, HeightField& sediment, const HeightField& base) const
	{
		for (int cz = 0; cz < coarse.depth_; ++cz)
		for (int cx = 0; cx < coarse.width_; ++cx)
		{
			const float w = tileWeight(cx, cz);
			if (w >= 1.f)
				continue;

			// Exactly the base where w is zero, not merely close to it: the
			// tile next door has to arrive at the same bits.
			const float b = base.at(cx, cz);
			coarse.at(cx, cz) = w <= 0.f ? b : b + w * (coarse.at(cx, cz) - b);
			sediment.at(cx, cz) = w <= 0.f ? 0.f : w * sediment.at(cx, cz);
		}
	}

	/*!
		@brief
			Restores pre-lake heights in the faded band and removes every lake that comes within a margin of it.
		@param coarse
			Heights after the lake pass, restored in the band
		@param lakes
			Water levels; lakes reaching the band are set to kNoWater
		@param beforeLakes
			Heights before the lake pass
		*/
	void TerrainGenerator::confineLakes(HeightField& coarse, HeightField& lakes, const HeightField& beforeLakes) const
	{
		const int width = lakes.width_, depth = lakes.depth_;
		const int cellVoxels = recipe_.erosion_.cellVoxels_;

		// Coarse cells a lake must keep between itself and the faded band.
		const int shore = static_cast<int>(std::ceil(recipe_.surface_.shoreReach_ / recipe_.voxelSize_)) + kLakeMarginVoxels;
		const int margin = (shore + cellVoxels - 1) / cellVoxels;

		const auto faded = [&](int cx, int cz) { return tileWeight(cx, cz) < 1.f; };

		for (int cz = 0; cz < depth; ++cz)
		for (int cx = 0; cx < width; ++cx)
			if (faded(cx, cz))
				coarse.at(cx, cz) = beforeLakes.at(cx, cz);

		// Lakes one at a time: a lake is a connected run of wet cells.
		std::vector<std::uint8_t> seen(lakes.size(), 0);
		std::vector<std::size_t> lake, stack;

		for (int z0 = 0; z0 < depth; ++z0)
		for (int x0 = 0; x0 < width; ++x0)
		{
			const std::size_t first = lakes.index(x0, z0);
			if (seen[first] || lakes.h_[first] <= kNoWater)
				continue;

			lake.clear();
			stack.assign(1, first);
			seen[first] = 1;
			bool reaches = false;

			while (!stack.empty())
			{
				const std::size_t i = stack.back();
				stack.pop_back();
				lake.push_back(i);

				const int x = static_cast<int>(i % static_cast<std::size_t>(width));
				const int z = static_cast<int>(i / static_cast<std::size_t>(width));

				// The band is convex towards the middle, so the four cells
				// margin away along the axes decide it.
				reaches = reaches
					|| faded(std::max(x - margin, 0), z) || faded(std::min(x + margin, width - 1), z)
					|| faded(x, std::max(z - margin, 0)) || faded(x, std::min(z + margin, depth - 1));

				constexpr int kSteps[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
				for (const auto& step : kSteps)
				{
					const int nx = x + step[0], nz = z + step[1];
					if (nx < 0 || nz < 0 || nx >= width || nz >= depth)
						continue;

					const std::size_t n = lakes.index(nx, nz);
					if (!seen[n] && lakes.h_[n] > kNoWater)
					{
						seen[n] = 1;
						stack.push_back(n);
					}
				}
			}

			if (reaches)
				for (const std::size_t i : lake)
					lakes.h_[i] = kNoWater;
		}
	}

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
	void TerrainGenerator::fillColumn(World& world, const TerrainMaps& maps, int sx, int sz) const
	{
		fillColumnInto(maps, sx * S, sz * S, world.sectionsAt(0).y,
			[&world, sx, sz](int sy) -> Section& { return world.editSection(0, sx, sy, sz); });
	}

	/*!
		@brief
			Writes one section column's voxels into free-standing sections.
		@param maps
			The prepass result
		@param x0
			The column's minimum corner in the maps' columns, along x
		@param z0
			The column's minimum corner in the maps' columns, along z
		@param column
			One section per 32 voxels of height, bottom first; every one is overwritten
		*/
	void TerrainGenerator::fillColumnSections(const TerrainMaps& maps, int x0, int z0, std::span<Section> column) const
	{
		fillColumnInto(maps, x0, z0, static_cast<int>(column.size()),
			[&column](int sy) -> Section& { return column[static_cast<std::size_t>(sy)]; });
	}

	/*!
		@brief
			Fills one section column from the maps: top, soil, biome rock or strata, base, caves and water.

			Sections above the tallest column are air, and sections wholly under
			the base depth (no caves) are filled uniform.
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
	void TerrainGenerator::fillColumnInto(const TerrainMaps& maps, int x0, int z0, int sectionsY, At&& at) const
	{
		const Recipe& r = recipe_;

		// Noise sampled by voxel is sampled by the voxel's place in the world:
		// a tile's maps begin somewhere in the middle of it.
		const glm::ivec2 noiseOffset = tile_ ? tile_->voxelOffset_ : glm::ivec2{ 0 };

		// The tallest thing in this column of sections, and the shallowest
		// depth at which everything below is base material.
		const bool banded = !maps.biome_.empty();
		int highest = -1, baseBelow = r.size_.y;
		for (int dz = 0; dz < S; ++dz)
		for (int dx = 0; dx < S; ++dx)
		{
			const int x = x0 + dx, z = z0 + dz;
			if (x >= r.size_.x || z >= r.size_.z)
				continue;

			const std::size_t i = maps.index(x, z);
			const int surface = maps.surface_[i];
			highest = std::max({ highest, surface, static_cast<int>(maps.waterTop_[i]) });
			// A biome with rock of its own keeps it in a shell under the soil;
			// only below that is everything the recipe's base.
			const int shell = banded && covers_[maps.biome_[i]].ownRock_ ? shellVoxels_ : 0;
			baseBelow = std::min(baseBelow, surface - maps.topDepth_[i] - maps.soilDepth_[i] - shell);
		}

		if (highest < 0)
		{
			for (int sy = 0; sy < sectionsY; ++sy)
				at(sy).fill(kAir);
			return;
		}

		const bool fullyInside = (x0 + S <= r.size_.x) && (z0 + S <= r.size_.z);

		for (int sy = 0; sy < sectionsY; ++sy)
		{
			const int yMin = sy * S, yMax = yMin + S - 1;
			Section& section = at(sy);

			if (yMin > highest)
			{
				section.fill(kAir);
				continue;
			}

			if (fullyInside && !r.caves_.enabled_ && yMax <= baseBelow)
			{
				section.fill(surface_.base_);
				continue;
			}

			section.fill(kAir);
			std::uint8_t* data = section.makeDense();

			for (int dz = 0; dz < S; ++dz)
			for (int dx = 0; dx < S; ++dx)
			{
				const int x = x0 + dx, z = z0 + dz;
				if (x >= r.size_.x || z >= r.size_.z)
					continue;

				const std::size_t i = maps.index(x, z);
				const int surface = maps.surface_[i];
				const int topFrom = surface - maps.topDepth_[i];		// exclusive lower bounds
				const int soilFrom = topFrom - maps.soilDepth_[i];
				const MaterialId top = maps.topMaterial_[i];

				const BiomeCover* cover = banded ? &covers_[maps.biome_[i]] : nullptr;
				const MaterialId under = cover ? cover->materials_.under_ : surface_.under_;
				const int shellFrom = cover && cover->ownRock_ ? soilFrom - shellVoxels_ : soilFrom;
				const int strataShift = banded ? maps.strataShift_[i] : 0;

				const int solidTop = std::min(surface, yMax);
				for (int y = yMin; y <= solidTop; ++y)
				{
					MaterialId m;
					if (y > topFrom)		m = top;
					else if (y > soilFrom)	m = under;
					else if (y > shellFrom)	m = cover->rockAt(y + strataShift);
					else					m = surface_.base_;

					if (r.caves_.enabled_ && y >= r.caves_.minY_ && y < surface - r.caves_.minDepthBelowSurface_)
					{
						const double n = util::noise::fbm3(seedCaves_, x + noiseOffset.x, y, z + noiseOffset.y, r.caves_.fbm_);
						if (n > r.caves_.threshold_)
							m = kAir;
					}

					data[voxelIndex(dx, y - yMin, dz)] = m;
				}

				const int waterTop = std::min(static_cast<int>(maps.waterTop_[i]), yMax);
				for (int y = std::max(yMin, surface + 1); y <= waterTop; ++y)
					data[voxelIndex(dx, y - yMin, dz)] = water_;
			}

			section.tryCollapse();
		}
	}

	/*!
		@brief
			Writes level 0 from maps, one job per section column when a queue is given.
		@param world
			Receives level 0; its dimensions must match the recipe
		@param maps
			The prepass result; must match the recipe's size
		@param jobs
			Queue to spread section columns over, or null
		@param progress
			Incremented once per column, if given
		@param cancel
			Polled before each column
		@return
			false if cancelled
		*/
	bool TerrainGenerator::fill(World& world, const TerrainMaps& maps, util::thread::JobQueue* jobs,
								std::atomic<int>* progress, const std::atomic<bool>* cancel) const
	{
		VOX_PROFILE_FUNCTION_C(profile::colour::kVoxel);

		const glm::ivec3 sections = world.sectionsAt(0);
		if (sections != dims().sections_)
			throw std::runtime_error("TerrainGenerator: world dimensions do not match the recipe");
		if (maps.width_ != recipe_.size_.x || maps.depth_ != recipe_.size_.z)
			throw std::runtime_error("TerrainGenerator: terrain maps do not match the recipe");

		const auto column = [&](int sx, int sz)
		{
			if (cancel && cancel->load(std::memory_order_relaxed))
				return;
			fillColumn(world, maps, sx, sz);
			if (progress)
				progress->fetch_add(1, std::memory_order_relaxed);
		};

		if (jobs == nullptr)
		{
			for (int sz = 0; sz < sections.z; ++sz)
			for (int sx = 0; sx < sections.x; ++sx)
			{
				if (cancel && cancel->load(std::memory_order_relaxed))
					return false;
				column(sx, sz);
			}
			return true;
		}

		// Each job owns one section column: disjoint writes, no locking.
		for (int sz = 0; sz < sections.z; ++sz)
		for (int sx = 0; sx < sections.x; ++sx)
			jobs->push([column, sx, sz] { column(sx, sz); });

		jobs->waitIdle();
		return !(cancel && cancel->load(std::memory_order_relaxed));
	}

	/*!
		@brief
			buildMaps() then fill(): fills level 0.
		@param world
			Receives level 0
		@param jobs
			Queue for the parallel passes, or null
		@param progress
			Incremented workUnits() times over a full run, if given
		@param cancel
			Polled between units
		@param observer
			Shown the terrain after each prepass stage, if given
		@return
			false if cancelled
		*/
	bool TerrainGenerator::generate(World& world, util::thread::JobQueue* jobs,
									std::atomic<int>* progress, const std::atomic<bool>* cancel,
									GenStageObserver* observer) const
	{
		VOX_PROFILE_FUNCTION_C(profile::colour::kVoxel);

		const std::optional<TerrainMaps> maps = buildMaps(jobs, progress, cancel, observer);
		if (!maps)
			return false;
		return fill(world, *maps, jobs, progress, cancel);
	}
}
