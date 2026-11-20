/*************************************************************************************************************
\file       recipe.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Implements strict .voxgen parsing into a Recipe (every block validated, unknown keys are
            errors) and the stable Recipe::toJson writer that round-trips it.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "recipe.hpp"

// Voxel
#include "json_read.hpp"
#include "biome/biome_json.hpp"
#include "biome/biome_map.hpp"

// stds
#include <stdexcept>


namespace voxel
{
	namespace
	{
		using namespace jsonread;
		using nlohmann::ordered_json;

		/*!
			@brief
				Reads the NoiseLayer keys of o; the caller has already vetted the key set.
			@param o
				JSON object holding the layer
			@param layer
				Overwritten with the values found; keys absent keep their current value
			@param where
				Name of the block, for error messages
			*/
		void layerOf(const json& o, NoiseLayer& layer, const std::string& where)
		{
			layer.wavelength_ = number<double>(o, "wavelength", layer.wavelength_, where);
			layer.amplitude_ = number<double>(o, "amplitude", layer.amplitude_, where);
			layer.octaves_ = number<int>(o, "octaves", layer.octaves_, where);
			layer.lacunarity_ = number<double>(o, "lacunarity", layer.lacunarity_, where);
			layer.gain_ = number<double>(o, "gain", layer.gain_, where);

			positive(layer.wavelength_, "wavelength", where);
			if (layer.octaves_ < 1 || layer.octaves_ > 16)
				fail("\"octaves\" in " + where + " must be 1..16");
			if (layer.lacunarity_ <= 1.0)
				fail("\"lacunarity\" in " + where + " must exceed 1");
			if (layer.amplitude_ < 0.0)
				fail("\"amplitude\" in " + where + " must be >= 0");
		}

		/*!
			@brief
				Reads and validates the fbm keys of the caves block.
			@param o
				The caves JSON object
			@return
				The cave noise parameters
			*/
		util::noise::FbmParams cavesFbmOf(const json& o)
		{
			util::noise::FbmParams p;
			p.octaves_ = number<int>(o, "octaves", p.octaves_, "caves");
			p.frequency_ = number<double>(o, "frequency", p.frequency_, "caves");
			p.lacunarity_ = number<double>(o, "lacunarity", p.lacunarity_, "caves");
			p.gain_ = number<double>(o, "gain", p.gain_, "caves");
			if (p.octaves_ < 1 || p.octaves_ > 16)
				fail("\"octaves\" in caves must be 1..16");
			positive(p.frequency_, "frequency", "caves");
			return p;
		}

		/*!
			@brief
				Reads and validates the shape block (base height, warp, massif, detail, ring, basins).
			@param s
				The shape JSON object
			@param shape
				Filled from the block; absent keys keep their defaults
			*/
		void shapeOf(const json& s, Recipe::Shape& shape)
		{
			onlyKeys(s, { "baseHeight", "warp", "massif", "detail", "ring", "basins" }, "shape");
			shape.baseHeight_ = number<float>(s, "baseHeight", shape.baseHeight_, "shape");

			if (const auto w = s.find("warp"); w != s.end())
			{
				onlyKeys(*w, { "wavelength", "amplitude", "octaves", "lacunarity", "gain" }, "shape.warp");
				layerOf(*w, shape.warp_, "shape.warp");
			}

			if (const auto m = s.find("massif"); m != s.end())
			{
				onlyKeys(*m, { "wavelength", "amplitude", "octaves", "lacunarity", "gain", "floor" }, "shape.massif");
				layerOf(*m, shape.massif_, "shape.massif");
				shape.massifFloor_ = number<float>(*m, "floor", shape.massifFloor_, "shape.massif");
				if (shape.massifFloor_ < 0.f || shape.massifFloor_ > 1.f)
					fail("\"floor\" in shape.massif must be 0..1");
			}

			if (const auto d = s.find("detail"); d != s.end())
			{
				onlyKeys(*d, { "wavelength", "amplitude", "octaves", "lacunarity", "gain", "damping", "interior" }, "shape.detail");
				layerOf(*d, shape.detail_, "shape.detail");
				shape.detailDamping_ = number<float>(*d, "damping", shape.detailDamping_, "shape.detail");
				shape.detailInterior_ = number<float>(*d, "interior", shape.detailInterior_, "shape.detail");
				if (shape.detailDamping_ < 0.f)
					fail("\"damping\" in shape.detail must be >= 0");
			}

			if (const auto r = s.find("ring"); r != s.end())
			{
				onlyKeys(*r, { "inner", "outer", "power", "wobble" }, "shape.ring");
				Recipe::Shape::Ring& ring = shape.ring_;
				ring.enabled_ = true;
				ring.inner_ = number<float>(*r, "inner", ring.inner_, "shape.ring");
				ring.outer_ = number<float>(*r, "outer", ring.outer_, "shape.ring");
				ring.power_ = number<float>(*r, "power", ring.power_, "shape.ring");
				ring.wobble_ = number<float>(*r, "wobble", ring.wobble_, "shape.ring");
				if (ring.outer_ <= ring.inner_)
					fail("\"outer\" in shape.ring must exceed \"inner\"");
				positive(ring.power_, "power", "shape.ring");
			}

			if (const auto b = s.find("basins"); b != s.end())
			{
				if (!b->is_array())
					fail("\"basins\" in shape must be an array");

				for (const json& entry : *b)
				{
					onlyKeys(entry, { "centre", "radius", "depth", "rim" }, "shape.basins[]");
					Recipe::Shape::Basin basin;

					if (const auto c = entry.find("centre"); c != entry.end())
					{
						if (!c->is_array() || c->size() != 2 || !(*c)[0].is_number() || !(*c)[1].is_number())
							fail("\"centre\" in shape.basins[] must be [x, z] in 0..1");
						basin.centre_ = { (*c)[0].get<float>(), (*c)[1].get<float>() };
					}

					basin.radius_ = number<float>(entry, "radius", basin.radius_, "shape.basins[]");
					basin.depth_ = number<float>(entry, "depth", basin.depth_, "shape.basins[]");
					basin.rim_ = number<float>(entry, "rim", basin.rim_, "shape.basins[]");
					positive(basin.radius_, "radius", "shape.basins[]");
					shape.basins_.push_back(basin);
				}
			}
		}

		/*!
			@brief
				Reads and validates the erosion block, including its thermal sub-block.
			@param e
				The erosion JSON object
			@param erosion
				Filled from the block; absent keys keep their defaults
			@param version
				Recipe version; minRelief needs version 3
			*/
		void erosionOf(const json& e, Recipe::Erosion& erosion, int version)
		{
			onlyKeys(e, { "cellVoxels", "droplets", "radius", "inertia", "capacity", "minCapacity", "erode", "deposit",
						  "evaporate", "gravity", "lifetime", "thermal", "minRelief" }, "erosion");

			if (version < 3 && e.find("minRelief") != e.end())
				fail("\"minRelief\" in erosion needs version 3");
			erosion.minRelief_ = number<float>(e, "minRelief", erosion.minRelief_, "erosion");
			if (erosion.minRelief_ < 0.f)
				fail("\"minRelief\" in erosion must be >= 0");

			erosion.cellVoxels_ = number<int>(e, "cellVoxels", erosion.cellVoxels_, "erosion");
			erosion.droplets_ = number<int>(e, "droplets", erosion.droplets_, "erosion");
			erosion.radius_ = number<int>(e, "radius", erosion.radius_, "erosion");
			erosion.inertia_ = number<float>(e, "inertia", erosion.inertia_, "erosion");
			erosion.capacity_ = number<float>(e, "capacity", erosion.capacity_, "erosion");
			erosion.minCapacity_ = number<float>(e, "minCapacity", erosion.minCapacity_, "erosion");
			erosion.erode_ = number<float>(e, "erode", erosion.erode_, "erosion");
			erosion.deposit_ = number<float>(e, "deposit", erosion.deposit_, "erosion");
			erosion.evaporate_ = number<float>(e, "evaporate", erosion.evaporate_, "erosion");
			erosion.gravity_ = number<float>(e, "gravity", erosion.gravity_, "erosion");
			erosion.lifetime_ = number<int>(e, "lifetime", erosion.lifetime_, "erosion");

			if (erosion.cellVoxels_ < 1 || erosion.cellVoxels_ > 16)
				fail("\"cellVoxels\" in erosion must be 1..16");
			if (erosion.droplets_ < 0 || erosion.droplets_ > 20000000)
				fail("\"droplets\" in erosion must be 0..20000000");
			if (erosion.radius_ < 1 || erosion.radius_ > 8)
				fail("\"radius\" in erosion must be 1..8");
			if (erosion.inertia_ < 0.f || erosion.inertia_ >= 1.f)
				fail("\"inertia\" in erosion must be in [0, 1)");
			if (erosion.lifetime_ < 1 || erosion.lifetime_ > 1000)
				fail("\"lifetime\" in erosion must be 1..1000");
			if (erosion.erode_ < 0.f || erosion.erode_ > 1.f || erosion.deposit_ < 0.f || erosion.deposit_ > 1.f
				|| erosion.evaporate_ < 0.f || erosion.evaporate_ >= 1.f)
				fail("\"erode\", \"deposit\" and \"evaporate\" in erosion must be 0..1");

			if (const auto t = e.find("thermal"); t != e.end())
			{
				onlyKeys(*t, { "iterations", "rockTalusDeg", "sedimentTalusDeg", "rate" }, "erosion.thermal");
				erosion.thermalIterations_ = number<int>(*t, "iterations", erosion.thermalIterations_, "erosion.thermal");
				erosion.rockTalusDeg_ = number<float>(*t, "rockTalusDeg", erosion.rockTalusDeg_, "erosion.thermal");
				erosion.sedimentTalusDeg_ = number<float>(*t, "sedimentTalusDeg", erosion.sedimentTalusDeg_, "erosion.thermal");
				erosion.thermalRate_ = number<float>(*t, "rate", erosion.thermalRate_, "erosion.thermal");

				if (erosion.thermalIterations_ < 0 || erosion.thermalIterations_ > 5000)
					fail("\"iterations\" in erosion.thermal must be 0..5000");
				slopeDeg(erosion.rockTalusDeg_, "rockTalusDeg", "erosion.thermal");
				slopeDeg(erosion.sedimentTalusDeg_, "sedimentTalusDeg", "erosion.thermal");
				if (erosion.thermalRate_ <= 0.f || erosion.thermalRate_ > 0.5f)
					fail("\"rate\" in erosion.thermal must be in (0, 0.5]");
			}
		}

		/*!
			@brief
				Reads and validates the surface block (cover materials, depths, slope and height rules).
			@param s
				The surface JSON object
			@param surface
				Filled from the block; absent keys keep their defaults
			*/
		void surfaceOf(const json& s, Recipe::Surface& surface)
		{
			onlyKeys(s, { "top", "under", "base", "sand", "snow", "soilDepth", "grassMaxSlopeDeg", "rockSlopeDeg",
						  "slopeJitterDeg", "jitterWavelength", "slopeReach", "snowline", "snowJitter", "snowMaxSlopeDeg", "snowDepth",
						  "sandDeposit", "sandMaxHeight", "shoreBand", "shoreReach", "sandDepth" }, "surface");

			surface.top_ = text(s, "top", surface.top_, "surface");
			surface.under_ = text(s, "under", surface.under_, "surface");
			surface.base_ = text(s, "base", surface.base_, "surface");
			surface.sand_ = text(s, "sand", surface.sand_, "surface");
			surface.snow_ = text(s, "snow", surface.snow_, "surface");

			surface.soilDepth_ = number<float>(s, "soilDepth", surface.soilDepth_, "surface");
			surface.grassMaxSlopeDeg_ = number<float>(s, "grassMaxSlopeDeg", surface.grassMaxSlopeDeg_, "surface");
			surface.rockSlopeDeg_ = number<float>(s, "rockSlopeDeg", surface.rockSlopeDeg_, "surface");
			surface.slopeJitterDeg_ = number<float>(s, "slopeJitterDeg", surface.slopeJitterDeg_, "surface");
			surface.jitterWavelength_ = number<float>(s, "jitterWavelength", surface.jitterWavelength_, "surface");
			surface.slopeReach_ = number<float>(s, "slopeReach", surface.slopeReach_, "surface");
		surface.snowline_ = number<float>(s, "snowline", surface.snowline_, "surface");
			surface.snowJitter_ = number<float>(s, "snowJitter", surface.snowJitter_, "surface");
			surface.snowMaxSlopeDeg_ = number<float>(s, "snowMaxSlopeDeg", surface.snowMaxSlopeDeg_, "surface");
			surface.snowDepth_ = number<float>(s, "snowDepth", surface.snowDepth_, "surface");
			surface.sandDeposit_ = number<float>(s, "sandDeposit", surface.sandDeposit_, "surface");
			surface.sandMaxHeight_ = number<float>(s, "sandMaxHeight", surface.sandMaxHeight_, "surface");
			surface.shoreBand_ = number<float>(s, "shoreBand", surface.shoreBand_, "surface");
			surface.shoreReach_ = number<float>(s, "shoreReach", surface.shoreReach_, "surface");
			surface.sandDepth_ = number<float>(s, "sandDepth", surface.sandDepth_, "surface");

			if (surface.soilDepth_ < 0.f || surface.snowDepth_ < 0.f || surface.sandDepth_ < 0.f)
				fail("depths in surface must be >= 0");
			if (surface.slopeJitterDeg_ < 0.f)
				fail("\"slopeJitterDeg\" in surface must be >= 0");
			positive(surface.jitterWavelength_, "jitterWavelength", "surface");
		positive(surface.slopeReach_, "slopeReach", "surface");
			slopeDeg(surface.grassMaxSlopeDeg_ - surface.slopeJitterDeg_, "grassMaxSlopeDeg", "surface");
			slopeDeg(surface.rockSlopeDeg_ + surface.slopeJitterDeg_, "rockSlopeDeg", "surface");
			slopeDeg(surface.snowMaxSlopeDeg_, "snowMaxSlopeDeg", "surface");
			if (surface.rockSlopeDeg_ < surface.grassMaxSlopeDeg_)
				fail("\"rockSlopeDeg\" in surface must not be below \"grassMaxSlopeDeg\"");
		}
	}

	/*!
		@brief
			Converts the layer to fbm parameters, keeping only octaves whose wavelength is still resolvable.
		@param minWavelength
			Octaves finer than this are dropped
		@return
			Parameters for inputs in metres
		*/
	util::noise::FbmParams NoiseLayer::fbm(double minWavelength) const
	{
		util::noise::FbmParams p;
		p.frequency_ = 1.0 / wavelength_;
		p.lacunarity_ = lacunarity_;
		p.gain_ = gain_;

		// Keep the octaves whose wavelength is still resolvable.
		p.octaves_ = 1;
		double wavelength = wavelength_ / lacunarity_;
		while (p.octaves_ < octaves_ && wavelength >= minWavelength)
		{
			++p.octaves_;
			wavelength /= lacunarity_;
		}
		return p;
	}

	namespace
	{
		/*!
			@brief
				Reads and validates the biomes block and its entries; enables biome terrain.
			@param b
				The biomes JSON object
			@param biomes
				Filled from the block
			*/
		void biomesOf(const json& b, Recipe::Biomes& biomes)
		{
			onlyKeys(b, { "size", "blending", "datumHeight", "reliefScale", "shellDepth", "entries" }, "biomes");

			biomes.enabled_ = true;
			biomes.size_ = number<float>(b, "size", biomes.size_, "biomes");
			biomes.blending_ = number<float>(b, "blending", biomes.blending_, "biomes");
			biomes.datumHeight_ = number<float>(b, "datumHeight", biomes.datumHeight_, "biomes");
			biomes.reliefScale_ = number<float>(b, "reliefScale", biomes.reliefScale_, "biomes");
			biomes.shellDepth_ = number<float>(b, "shellDepth", biomes.shellDepth_, "biomes");

			positive(biomes.size_, "size", "biomes");
			unit(biomes.blending_, "blending", "biomes");
			if (biomes.reliefScale_ <= 0.f || biomes.reliefScale_ > 1.f)
				fail("\"reliefScale\" in biomes must be in (0, 1]");
			if (biomes.shellDepth_ < 0.f || biomes.shellDepth_ > 25.f)
				fail("\"shellDepth\" in biomes must be 0..25");

			const auto e = b.find("entries");
			if (e == b.end() || !e->is_array() || e->empty())
				fail("biomes needs a non-empty \"entries\" array");
			if (e->size() > kMaxBiomes)
				fail("biomes holds at most " + std::to_string(kMaxBiomes) + " entries");

			for (std::size_t i = 0; i < e->size(); ++i)
			{
				BiomeDefinition biome = biomeFromJson((*e)[i], "biomes.entries[" + std::to_string(i) + "]");
				for (const BiomeDefinition& other : biomes.entries_)
					if (other.name_ == biome.name_)
						fail("biome \"" + biome.name_ + "\" appears twice in biomes.entries");
				biomes.entries_.push_back(std::move(biome));
			}
		}

		/*!
			@brief
				Builds a Recipe from a parsed .voxgen document, validating every block.
			@param doc
				The document root
			@return
				The recipe
			*/
		Recipe recipeOf(const json& doc)
		{
			if (!doc.is_object())
				fail("root must be an object");

			const int version = doc.value("version", 0);
			if (version == 1)
				fail("version 1 recipes (noise / splines) are no longer supported; see Assets/maps/arena01.voxgen for version 2");
			if (version != 2 && version != 3)
				fail("unsupported version");

			onlyKeys(doc, { "version", "seed", "voxelSize", "size", "origin", "shape", "biomes", "erosion", "water",
							"detailNoise", "surface", "caves", "stream" }, "root");

			Recipe r;
			r.seed_ = number<std::uint64_t>(doc, "seed", 0, "root");
			r.voxelSize_ = number<float>(doc, "voxelSize", 0.1f, "root");
			if (r.voxelSize_ <= 0.f)
				fail("\"voxelSize\" must be positive");

			if (const auto s = doc.find("size"); s != doc.end())
			{
				onlyKeys(*s, { "x", "y", "z" }, "size");
				r.size_ = { number<int>(*s, "x", r.size_.x, "size"), number<int>(*s, "y", r.size_.y, "size"), number<int>(*s, "z", r.size_.z, "size") };
				if (r.size_.x < 1 || r.size_.y < 2 || r.size_.z < 1 || r.size_.x > 65536 || r.size_.y > 4096 || r.size_.z > 65536)
					fail("\"size\" out of range");
			}

			if (const auto o = doc.find("origin"); o != doc.end())
			{
				if (!o->is_array() || o->size() != 3 || !(*o)[0].is_number() || !(*o)[1].is_number() || !(*o)[2].is_number())
					fail("\"origin\" must be [x, y, z]");
				r.origin_ = { (*o)[0].get<float>(), (*o)[1].get<float>(), (*o)[2].get<float>() };
			}

			if (const auto b = doc.find("biomes"); b != doc.end())
			{
				if (version < 3)
					fail("\"biomes\" needs version 3");
				if (doc.find("shape") != doc.end())
					fail("\"shape\" and \"biomes\" both describe the landform; a recipe has one or the other");
				biomesOf(*b, r.biomes_);
			}

			if (const auto s = doc.find("shape"); s != doc.end())
				shapeOf(*s, r.shape_);

			if (const auto e = doc.find("erosion"); e != doc.end())
				erosionOf(*e, r.erosion_, version);

			if (const auto w = doc.find("water"); w != doc.end())
			{
				onlyKeys(*w, { "enabled", "material", "minDepth", "minArea", "maxDepth", "fillFraction", "maxArea" }, "water");
				r.water_.enabled_ = flag(*w, "enabled", true, "water");
				r.water_.material_ = text(*w, "material", r.water_.material_, "water");
				r.water_.minDepth_ = number<float>(*w, "minDepth", r.water_.minDepth_, "water");
				r.water_.minArea_ = number<float>(*w, "minArea", r.water_.minArea_, "water");
				r.water_.maxDepth_ = number<float>(*w, "maxDepth", r.water_.maxDepth_, "water");
				r.water_.fillFraction_ = number<float>(*w, "fillFraction", r.water_.fillFraction_, "water");
				if (r.water_.fillFraction_ <= 0.f || r.water_.fillFraction_ > 1.f)
					fail("\"fillFraction\" in water must be in (0, 1]");
				positive(r.water_.maxDepth_, "maxDepth", "water");
				r.water_.maxArea_ = number<float>(*w, "maxArea", r.water_.maxArea_, "water");
				if (r.water_.maxArea_ < 0.f)
					fail("\"maxArea\" in water must be >= 0");
			}

			if (const auto d = doc.find("detailNoise"); d != doc.end())
			{
				onlyKeys(*d, { "wavelength", "amplitude", "octaves", "lacunarity", "gain", "fullSlopeDeg" }, "detailNoise");
				layerOf(*d, r.detailNoise_.layer_, "detailNoise");
				r.detailNoise_.fullSlopeDeg_ = number<float>(*d, "fullSlopeDeg", r.detailNoise_.fullSlopeDeg_, "detailNoise");
				slopeDeg(r.detailNoise_.fullSlopeDeg_, "fullSlopeDeg", "detailNoise");
			}

			if (const auto s = doc.find("surface"); s != doc.end())
				surfaceOf(*s, r.surface_);

			if (const auto c = doc.find("caves"); c != doc.end())
			{
				onlyKeys(*c, { "enabled", "frequency", "octaves", "lacunarity", "gain", "threshold", "minDepthBelowSurface", "minY" }, "caves");
				r.caves_.enabled_ = flag(*c, "enabled", false, "caves");
				r.caves_.fbm_ = cavesFbmOf(*c);
				r.caves_.threshold_ = number<double>(*c, "threshold", r.caves_.threshold_, "caves");
				r.caves_.minDepthBelowSurface_ = number<int>(*c, "minDepthBelowSurface", r.caves_.minDepthBelowSurface_, "caves");
				r.caves_.minY_ = number<int>(*c, "minY", r.caves_.minY_, "caves");
			}

			if (const auto s = doc.find("stream"); s != doc.end())
			{
				onlyKeys(*s, { "endless", "tileCells" }, "stream");
				r.stream_.endless_ = flag(*s, "endless", false, "stream");
				r.stream_.tileCells_ = number<int>(*s, "tileCells", r.stream_.tileCells_, "stream");

				if (r.stream_.tileCells_ != 1 && r.stream_.tileCells_ != 2 && r.stream_.tileCells_ != 4)
					fail("\"stream.tileCells\" must be 1, 2 or 4");
			}

			if (r.stream_.endless_)
			{
				if (!r.biomes_.enabled_)
					fail("\"stream.endless\" needs biome terrain (version 3 with \"biomes\"): a shaped landform cannot be made in tiles");

				// Whole tiles, so no cell is ever half inside the world.
				const int tile = r.stream_.tileCells_ * 256;
				if (r.size_.x % tile != 0 || r.size_.z % tile != 0)
					fail("an endless terrain's \"size\" x and z must be multiples of its tile (stream.tileCells x 256 voxels)");
				if (r.size_.x > Recipe::Stream::kMaxEndlessVoxels || r.size_.z > Recipe::Stream::kMaxEndlessVoxels)
					fail("an endless terrain's \"size\" x and z may not exceed 32768 voxels");
			}

			return r;
		}

		/*!
			@brief
				Writes a NoiseLayer's five keys as a JSON object.
			@param layer
				The layer to write
			@return
				The JSON object
			*/
		ordered_json layerToJson(const NoiseLayer& layer)
		{
			return { { "wavelength", layer.wavelength_ }, { "amplitude", layer.amplitude_ }, { "octaves", layer.octaves_ },
					 { "lacunarity", layer.lacunarity_ }, { "gain", layer.gain_ } };
		}
	}

	/*!
		@brief
			Parses a .voxgen (version 2 or 3); errors are rethrown with a ".voxgen: " prefix.
		@param bytes
			The file's bytes
		@return
			The parsed recipe
		*/
	Recipe Recipe::parse(std::span<const unsigned char> bytes)
	{
		try
		{
			return recipeOf(json::parse(bytes.begin(), bytes.end()));
		}
		catch (const std::exception& e)
		{
			throw std::runtime_error(std::string(".voxgen: ") + e.what());
		}
	}

	/*!
		@brief
			Writes the recipe as stable .voxgen text (version 3 when biomes or minRelief need it, else 2).
		@return
			The JSON text, indented by 2, ending in a newline
		*/
	std::string Recipe::toJson() const
	{
		ordered_json doc;
		doc["version"] = (biomes_.enabled_ || erosion_.minRelief_ > 0.f) ? 3 : 2;
		doc["seed"] = seed_;
		doc["voxelSize"] = tidy(voxelSize_);
		doc["size"] = { { "x", size_.x }, { "y", size_.y }, { "z", size_.z } };
		doc["origin"] = { tidy(origin_.x), tidy(origin_.y), tidy(origin_.z) };

		if (biomes_.enabled_)
		{
			ordered_json b;
			b["size"] = tidy(biomes_.size_);
			b["blending"] = tidy(biomes_.blending_);
			b["datumHeight"] = tidy(biomes_.datumHeight_);
			b["reliefScale"] = tidy(biomes_.reliefScale_);
			b["shellDepth"] = tidy(biomes_.shellDepth_);
			b["entries"] = ordered_json::array();
			for (const BiomeDefinition& biome : biomes_.entries_)
				b["entries"].push_back(biomeToJson(biome));
			doc["biomes"] = std::move(b);
		}
		else
		{
			ordered_json s;
			s["baseHeight"] = tidy(shape_.baseHeight_);
			s["warp"] = layerToJson(shape_.warp_);
			s["massif"] = layerToJson(shape_.massif_);
			s["massif"]["floor"] = tidy(shape_.massifFloor_);
			s["detail"] = layerToJson(shape_.detail_);
			s["detail"]["damping"] = tidy(shape_.detailDamping_);
			s["detail"]["interior"] = tidy(shape_.detailInterior_);
			if (shape_.ring_.enabled_)
				s["ring"] = { { "inner", tidy(shape_.ring_.inner_) }, { "outer", tidy(shape_.ring_.outer_) },
							  { "power", tidy(shape_.ring_.power_) }, { "wobble", tidy(shape_.ring_.wobble_) } };
			if (!shape_.basins_.empty())
			{
				s["basins"] = ordered_json::array();
				for (const Shape::Basin& basin : shape_.basins_)
					s["basins"].push_back({ { "centre", { tidy(basin.centre_.x), tidy(basin.centre_.y) } },
											{ "radius", tidy(basin.radius_) }, { "depth", tidy(basin.depth_) }, { "rim", tidy(basin.rim_) } });
			}
			doc["shape"] = std::move(s);
		}

		ordered_json e;
		e["cellVoxels"] = erosion_.cellVoxels_;
		e["droplets"] = erosion_.droplets_;
		e["radius"] = erosion_.radius_;
		e["inertia"] = tidy(erosion_.inertia_);
		e["capacity"] = tidy(erosion_.capacity_);
		e["minCapacity"] = tidy(erosion_.minCapacity_);
		e["erode"] = tidy(erosion_.erode_);
		e["deposit"] = tidy(erosion_.deposit_);
		e["evaporate"] = tidy(erosion_.evaporate_);
		e["gravity"] = tidy(erosion_.gravity_);
		e["lifetime"] = erosion_.lifetime_;
		if (erosion_.minRelief_ > 0.f)
			e["minRelief"] = tidy(erosion_.minRelief_);
		e["thermal"] = { { "iterations", erosion_.thermalIterations_ }, { "rockTalusDeg", tidy(erosion_.rockTalusDeg_) },
						 { "sedimentTalusDeg", tidy(erosion_.sedimentTalusDeg_) }, { "rate", tidy(erosion_.thermalRate_) } };
		doc["erosion"] = std::move(e);

		doc["water"] = { { "enabled", water_.enabled_ }, { "material", water_.material_ }, { "minDepth", tidy(water_.minDepth_) },
						 { "minArea", tidy(water_.minArea_) }, { "maxDepth", tidy(water_.maxDepth_) },
						 { "fillFraction", tidy(water_.fillFraction_) } };
		if (water_.maxArea_ > 0.f)
			doc["water"]["maxArea"] = tidy(water_.maxArea_);

		doc["detailNoise"] = layerToJson(detailNoise_.layer_);
		doc["detailNoise"]["fullSlopeDeg"] = tidy(detailNoise_.fullSlopeDeg_);

		const Surface& s = surface_;
		doc["surface"] = { { "top", s.top_ }, { "under", s.under_ }, { "base", s.base_ }, { "sand", s.sand_ }, { "snow", s.snow_ },
						   { "soilDepth", tidy(s.soilDepth_) }, { "grassMaxSlopeDeg", tidy(s.grassMaxSlopeDeg_) },
						   { "rockSlopeDeg", tidy(s.rockSlopeDeg_) }, { "slopeJitterDeg", tidy(s.slopeJitterDeg_) },
						   { "jitterWavelength", tidy(s.jitterWavelength_) }, { "slopeReach", tidy(s.slopeReach_) },
						   { "snowline", tidy(s.snowline_) }, { "snowJitter", tidy(s.snowJitter_) },
						   { "snowMaxSlopeDeg", tidy(s.snowMaxSlopeDeg_) }, { "snowDepth", tidy(s.snowDepth_) },
						   { "sandDeposit", tidy(s.sandDeposit_) }, { "sandMaxHeight", tidy(s.sandMaxHeight_) },
						   { "shoreBand", tidy(s.shoreBand_) }, { "shoreReach", tidy(s.shoreReach_) }, { "sandDepth", tidy(s.sandDepth_) } };

		doc["caves"] = { { "enabled", caves_.enabled_ }, { "frequency", caves_.fbm_.frequency_ }, { "octaves", caves_.fbm_.octaves_ },
						 { "lacunarity", caves_.fbm_.lacunarity_ }, { "gain", caves_.fbm_.gain_ }, { "threshold", caves_.threshold_ },
						 { "minDepthBelowSurface", caves_.minDepthBelowSurface_ }, { "minY", caves_.minY_ } };

		// Only when it says something: every recipe written before streaming
		// existed keeps its text, byte for byte.
		if (stream_ != Stream{})
			doc["stream"] = { { "endless", stream_.endless_ }, { "tileCells", stream_.tileCells_ } };

		return doc.dump(2) + "\n";
	}
}
