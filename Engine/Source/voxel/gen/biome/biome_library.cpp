/*************************************************************************************************************
\file       biome_library.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Parses the biome library (biomes.json) and composes a biome recipe from the user's generate
            settings: validation of sizes, biome selection, erosion effort and fitting relief to height.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "biome_library.hpp"

// Voxel
#include "biome_json.hpp"
#include "biome_map.hpp"
#include "biome_terrain.hpp"
#include "../json_read.hpp"
#include "../../world/world.hpp"

// stds
#include <algorithm>
#include <cmath>
#include <stdexcept>


namespace voxel
{
	namespace
	{
		using namespace jsonread;

		// Bounds taken from the ops' ranges assume every layer peaks at once,
		// which noise never does; this share of them is what terrain reaches.
		constexpr double kReachedShare = 0.8;

		// Ground kept under the lowest landform (room to dig, lake beds) and
		// air kept over the highest, as metres and as shares of the height.
		constexpr double kFloorMetres = 3.0, kFloorShare = 0.10;
		constexpr double kHeadroomMetres = 1.5, kHeadroomShare = 0.05;

		// Where in the spare height the landforms sit: nearer the floor.
		constexpr double kSpareBelow = 0.25;

		// Deposited sediment turns to sand only this far above the datum.
		constexpr float kSandAboveDatum = 3.f;
	}

	/*!
		@brief
			Parses a biome library: generate settings, default recipe blocks and the biome list.

			Errors are rethrown as std::runtime_error prefixed with "biomes.json: ".
		@param bytes
			The file's bytes
		@return
			The parsed library
		*/
	BiomeLibrary BiomeLibrary::parse(std::span<const unsigned char> bytes)
	{
		try
		{
			const json doc = json::parse(bytes.begin(), bytes.end());
			onlyKeys(doc, { "version", "generate", "defaults", "biomes" }, "root");
			if (doc.value("version", 0) != 1)
				fail("unsupported version");

			BiomeLibrary library;
			float shellDepth = library.defaults_.biomes_.shellDepth_;

			if (const auto g = doc.find("generate"); g != doc.end())
			{
				onlyKeys(*g, { "dropletsPerSquareMetre", "shellDepth" }, "generate");
				library.dropletsPerSquareMetre_ = number<float>(*g, "dropletsPerSquareMetre", library.dropletsPerSquareMetre_, "generate");
				shellDepth = number<float>(*g, "shellDepth", shellDepth, "generate");
				if (library.dropletsPerSquareMetre_ < 0.f)
					fail("\"dropletsPerSquareMetre\" in generate must be >= 0");
			}

			// The defaults are recipe blocks, so the recipe parser reads them.
			if (const auto d = doc.find("defaults"); d != doc.end())
			{
				onlyKeys(*d, { "erosion", "water", "detailNoise", "surface" }, "defaults");
				json recipe = *d;
				recipe["version"] = 3;
				const std::string text = recipe.dump();
				try
				{
					library.defaults_ = Recipe::parse({ reinterpret_cast<const unsigned char*>(text.data()), text.size() });
				}
				catch (const std::exception& e)
				{
					fail(std::string("defaults: ") + e.what());
				}
			}
			library.defaults_.biomes_.shellDepth_ = shellDepth;

			const auto b = doc.find("biomes");
			if (b == doc.end() || !b->is_array() || b->empty())
				fail("needs a non-empty \"biomes\" array");

			for (std::size_t i = 0; i < b->size(); ++i)
			{
				BiomeDefinition biome = biomeFromJson((*b)[i], "biomes[" + std::to_string(i) + "]");
				if (library.find(biome.name_) != nullptr)
					fail("biome \"" + biome.name_ + "\" is defined twice");
				library.biomes_.push_back(std::move(biome));
			}

			return library;
		}
		catch (const std::exception& e)
		{
			throw std::runtime_error(std::string("biomes.json: ") + e.what());
		}
	}

	/*!
		@brief
			Looks a biome up by name with a linear search.
		@param name
			The biome's name
		@return
			The biome, or nullptr when there is no biome of that name
		*/
	const BiomeDefinition* BiomeLibrary::find(std::string_view name) const
	{
		for (const BiomeDefinition& biome : biomes_)
			if (biome.name_ == name)
				return &biome;
		return nullptr;
	}

	/*!
		@brief
			Turns the user's choices into a complete version-3 recipe.

			Validates the size, picks the biomes in library order, scales the
			droplet count to the area (one tile for a streamed terrain), and fits
			the datum and relief scale to the height available. Throws
			std::runtime_error on invalid settings.
		@param library
			The biome library to take definitions and defaults from
		@param settings
			The user's choices
		@return
			The composed recipe
		*/
	Recipe composeBiomeRecipe(const BiomeLibrary& library, const GenerateSettings& settings)
	{
		using L = GenerateLimits;

		glm::ivec3 size = settings.size_;

		// A streamed terrain is made a tile at a time, so its extent is not
		// what costs: only the tile is. Whole tiles, so no cell is cut in two.
		const int tileVoxels = settings.tileCells_ * World::kCellSections * kSectionSize;
		if (settings.endless_)
		{
			if (settings.tileCells_ != 1 && settings.tileCells_ != 2 && settings.tileCells_ != 4)
				throw std::runtime_error("a streamed terrain's tiles are 1, 2 or 4 cells a side");

			size.x = std::max(1, (size.x + tileVoxels - 1) / tileVoxels) * tileVoxels;
			size.z = std::max(1, (size.z + tileVoxels - 1) / tileVoxels) * tileVoxels;

			if (size.x > Recipe::Stream::kMaxEndlessVoxels || size.z > Recipe::Stream::kMaxEndlessVoxels)
				throw std::runtime_error("a streamed terrain reaches at most "
					+ std::to_string(Recipe::Stream::kMaxEndlessVoxels) + " voxels a side");
		}
		else
		{
			if (size.x < L::kMinHorizontal || size.z < L::kMinHorizontal || size.x > L::kMaxHorizontal || size.z > L::kMaxHorizontal)
				throw std::runtime_error("terrain width and depth must be " + std::to_string(L::kMinHorizontal) + ".."
					+ std::to_string(L::kMaxHorizontal) + " voxels");
			if (static_cast<std::int64_t>(size.x) * size.z > L::kMaxColumns)
				throw std::runtime_error("terrain covers too many columns; shrink its width or depth");
		}

		if (size.y < L::kMinVertical || size.y > L::kMaxVertical)
			throw std::runtime_error("terrain height must be " + std::to_string(L::kMinVertical) + ".."
				+ std::to_string(L::kMaxVertical) + " voxels");
		if (!(settings.voxelSize_ > 0.f))
			throw std::runtime_error("voxel size must be positive");

		Recipe r = library.defaults();
		r.seed_ = settings.seed_;
		r.voxelSize_ = settings.voxelSize_;
		r.size_ = size;
		r.origin_ = glm::round(settings.origin_ / settings.voxelSize_) * settings.voxelSize_;
		r.shape_ = {};

		// Library order, whatever order they were ticked in: the same choice
		// must always give the same recipe.
		Recipe::Biomes& biomes = r.biomes_;
		biomes.enabled_ = true;
		biomes.size_ = std::clamp(settings.biomeSize_, L::kMinBiomeSize, L::kMaxBiomeSize);
		biomes.blending_ = std::clamp(settings.blending_, 0.f, 1.f);
		biomes.entries_.clear();

		for (const std::string& name : settings.biomes_)
			if (library.find(name) == nullptr)
				throw std::runtime_error("there is no biome named \"" + name + "\"");
		for (const BiomeDefinition& biome : library.biomes())
			if (std::find(settings.biomes_.begin(), settings.biomes_.end(), biome.name_) != settings.biomes_.end())
				biomes.entries_.push_back(biome);

		if (biomes.entries_.empty())
			throw std::runtime_error("choose at least one biome");
		if (biomes.entries_.size() > kMaxBiomes)
			throw std::runtime_error("a terrain mixes at most " + std::to_string(kMaxBiomes) + " biomes");

		// Erosion effort follows the area, as a concrete count: the recipe
		// alone has to say how much work generating it is.
		// For a streamed terrain the count is for one tile, which is the unit
		// that gets eroded.
		const double side = static_cast<double>(tileVoxels) * settings.voxelSize_;
		const double area = settings.endless_
			? side * side
			: static_cast<double>(size.x) * size.z * settings.voxelSize_ * settings.voxelSize_;
		r.erosion_.droplets_ = static_cast<int>(std::clamp(std::round(area * library.dropletsPerSquareMetre()), 0.0, 20000000.0));

		r.stream_ = {};
		r.stream_.endless_ = settings.endless_;
		if (settings.endless_)
			r.stream_.tileCells_ = settings.tileCells_;

		// Fit the landforms into the height there is: squeeze them when the
		// map is low, and sit them nearer the floor than the ceiling.
		double lowest = 1e30, highest = -1e30;
		double boundLow = 1e30, boundHigh = -1e30;
		const double minWavelength = 4.0 * r.erosion_.cellVoxels_ * settings.voxelSize_;
		for (const BiomeDefinition& biome : biomes.entries_)
		{
			const CompiledBiome compiled(biome, 0, minWavelength, landformScale(biome, biomes.size_));
			lowest = std::min(lowest, compiled.base() + kReachedShare * (compiled.low() - compiled.base()));
			highest = std::max(highest, compiled.base() + kReachedShare * (compiled.high() - compiled.base()));
			boundLow = std::min(boundLow, compiled.low());
			boundHigh = std::max(boundHigh, compiled.high());
		}

		const double height = static_cast<double>(size.y) * settings.voxelSize_;
		const double floor = std::max(kFloorMetres, kFloorShare * height);
		const double headroom = std::max(kHeadroomMetres, kHeadroomShare * height);
		const double usable = std::max(1.0, height - floor - headroom);
		const double span = std::max(1e-3, highest - lowest);

		const double scale = std::min(1.0, usable / span);
		const double spare = usable - span * scale;

		// Two decimals: the numbers are written into the recipe for people to read.
		biomes.reliefScale_ = static_cast<float>(std::max(0.01, std::floor(scale * 100.0) / 100.0));
		biomes.datumHeight_ = static_cast<float>(std::round((floor + kSpareBelow * spare - lowest * biomes.reliefScale_) * 100.0) / 100.0);

		r.surface_.sandMaxHeight_ = biomes.datumHeight_ + kSandAboveDatum;

		// The droplet pass scales the ground to 0..1 by the relief it finds. A
		// map has one relief; a streamed terrain would have one per tile, and
		// the same hill would wash away more in a flat tile than in a rugged
		// one. The most the chosen biomes can span is the same everywhere.
		if (settings.endless_)
		{
			r.erosion_.cellVoxels_ = 2;		// a power of two: the upsample at a seam is then exact
			r.erosion_.minRelief_ = static_cast<float>(std::ceil(std::max(1.0, (boundHigh - boundLow) * biomes.reliefScale_)));
		}

		return r;
	}
}
