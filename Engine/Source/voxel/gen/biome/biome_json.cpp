/*************************************************************************************************************
\file       biome_json.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Parses and writes biome definitions and their height layers as JSON, validating keys,
            height ops, op parameters and value ranges.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "biome_json.hpp"

// Voxel
#include "height_ops.hpp"
#include "../json_read.hpp"

// stds
#include <algorithm>
#include <cmath>
#include <iterator>


namespace voxel
{
	namespace
	{
		using namespace jsonread;
		using nlohmann::ordered_json;

		constexpr const char* kLayerKeys[] = { "op", "wavelength", "amplitude", "octaves", "lacunarity", "gain", "warp", "mask" };

		/*!
			@brief
				Parses one height layer, accepting the shared keys plus the parameters its op declares.
			@param o
				The JSON object holding the layer
			@param where
				How error messages name this layer
			@return
				The parsed height layer
			*/
		HeightLayer layerFromJson(const json& o, const std::string& where)
		{
			if (!o.is_object())
				fail(where + " must be an object");

			HeightLayer layer;
			layer.op_ = text(o, "op", layer.op_, where);

			const HeightOp* op = findHeightOp(layer.op_);
			if (op == nullptr)
			{
				std::string known;
				for (const HeightOp& candidate : heightOps())
					known += std::string(known.empty() ? "" : ", ") + candidate.name_;
				fail("unknown op \"" + layer.op_ + "\" in " + where + " (known: " + known + ")");
			}

			// The shared keys, plus whatever this op declares.
			for (const auto& [key, value] : o.items())
			{
				bool ok = std::any_of(std::begin(kLayerKeys), std::end(kLayerKeys), [&](const char* k) { return key == k; });
				for (std::size_t p = 0; !ok && p < op->paramCount_; ++p)
					ok = key == op->params_[p].name_;
				if (!ok)
					fail("unknown key \"" + key + "\" in " + where + " (op \"" + layer.op_ + "\")");
			}

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

			if (const auto w = o.find("warp"); w != o.end())
			{
				onlyKeys(*w, { "wavelength", "amplitude" }, where + ".warp");
				layer.warp_.wavelength_ = number<double>(*w, "wavelength", layer.warp_.wavelength_, where + ".warp");
				layer.warp_.amplitude_ = number<double>(*w, "amplitude", layer.warp_.amplitude_, where + ".warp");
				positive(layer.warp_.wavelength_, "wavelength", where + ".warp");

				// What is not written back must not survive in memory either, or
				// a recipe would differ from its own saved text.
				if (layer.warp_.amplitude_ == 0.0)
					layer.warp_ = {};
			}

			if (const auto m = o.find("mask"); m != o.end())
			{
				onlyKeys(*m, { "wavelength", "floor" }, where + ".mask");
				layer.mask_.wavelength_ = number<double>(*m, "wavelength", layer.mask_.wavelength_, where + ".mask");
				layer.mask_.floor_ = number<double>(*m, "floor", layer.mask_.floor_, where + ".mask");
				positive(layer.mask_.wavelength_, "wavelength", where + ".mask");
				unit(layer.mask_.floor_, "floor", where + ".mask");
			}

			for (std::size_t p = 0; p < op->paramCount_; ++p)
			{
				const HeightOpParam& spec = op->params_[p];
				if (o.find(spec.name_) == o.end())
					continue;

				const double value = number<double>(o, spec.name_, spec.fallback_, where);
				if (!(value >= spec.min_ && value <= spec.max_))
					fail(std::string("\"") + spec.name_ + "\" in " + where + " must be "
						+ std::to_string(spec.min_) + ".." + std::to_string(spec.max_));
				layer.params_[spec.name_] = value;
			}

			return layer;
		}

		/*!
			@brief
				Writes one height layer as JSON, omitting warp and mask when they are off.
			@param layer
				The layer to write
			@return
				The layer as an ordered JSON object
			*/
		ordered_json layerToJson(const HeightLayer& layer)
		{
			ordered_json o;
			o["op"] = layer.op_;
			o["wavelength"] = layer.wavelength_;
			o["amplitude"] = layer.amplitude_;
			o["octaves"] = layer.octaves_;
			o["lacunarity"] = layer.lacunarity_;
			o["gain"] = layer.gain_;
			for (const auto& [name, value] : layer.params_)
				o[name] = value;
			if (layer.warp_.amplitude_ != 0.0)
				o["warp"] = { { "wavelength", layer.warp_.wavelength_ }, { "amplitude", layer.warp_.amplitude_ } };
			if (layer.mask_.wavelength_ > 0.0)
				o["mask"] = { { "wavelength", layer.mask_.wavelength_ }, { "floor", layer.mask_.floor_ } };
			return o;
		}
	}

	/*!
		@brief
			Parses a biome definition from a JSON object; unknown keys and out-of-range values throw.
		@param o
			The JSON object holding the biome
		@param where
			How error messages name this object, e.g. "biomes[2]"
		@return
			The parsed biome definition
		*/
	BiomeDefinition biomeFromJson(const json& o, const std::string& where)
	{
		onlyKeys(o, { "name", "colour", "featureSize", "base", "layers", "terrace", "edgeRelief", "erodibility", "talusDeg",
					  "palette", "strata", "rules" }, where);

		BiomeDefinition biome;
		biome.name_ = text(o, "name", "", where);
		if (biome.name_.empty())
			fail(where + " needs a \"name\"");

		if (const auto c = o.find("colour"); c != o.end())
		{
			if (!c->is_array() || c->size() != 3 || !(*c)[0].is_number() || !(*c)[1].is_number() || !(*c)[2].is_number())
				fail("\"colour\" in " + where + " must be [r, g, b] in 0..255");
			biome.colour_ = glm::clamp(glm::vec3{ (*c)[0].get<float>(), (*c)[1].get<float>(), (*c)[2].get<float>() } / 255.f, 0.f, 1.f);
		}

		biome.featureSize_ = number<float>(o, "featureSize", biome.featureSize_, where);
		if (biome.featureSize_ < 0.f)
			fail("\"featureSize\" in " + where + " must be >= 0");

		biome.base_ = number<float>(o, "base", biome.base_, where);
		biome.edgeRelief_ = number<float>(o, "edgeRelief", biome.edgeRelief_, where);
		biome.erodibility_ = number<float>(o, "erodibility", biome.erodibility_, where);
		biome.talusDeg_ = number<float>(o, "talusDeg", biome.talusDeg_, where);
		unit(biome.edgeRelief_, "edgeRelief", where);
		unit(biome.erodibility_, "erodibility", where);
		slopeDeg(biome.talusDeg_, "talusDeg", where);

		if (const auto l = o.find("layers"); l != o.end())
		{
			if (!l->is_array())
				fail("\"layers\" in " + where + " must be an array");
			for (std::size_t i = 0; i < l->size(); ++i)
				biome.layers_.push_back(layerFromJson((*l)[i], where + ".layers[" + std::to_string(i) + "]"));
		}

		if (const auto t = o.find("terrace"); t != o.end())
		{
			onlyKeys(*t, { "step", "sharpness", "slack" }, where + ".terrace");
			biome.terrace_.step_ = number<float>(*t, "step", biome.terrace_.step_, where + ".terrace");
			biome.terrace_.sharpness_ = number<float>(*t, "sharpness", biome.terrace_.sharpness_, where + ".terrace");
			biome.terrace_.slack_ = number<float>(*t, "slack", biome.terrace_.slack_, where + ".terrace");
			unit(biome.terrace_.slack_, "slack", where + ".terrace");
			if (biome.terrace_.step_ < 0.f)
				fail("\"step\" in " + where + ".terrace must be >= 0");
			if (biome.terrace_.sharpness_ < 1.f)
				fail("\"sharpness\" in " + where + ".terrace must be >= 1");
			if (biome.terrace_.step_ == 0.f)
				biome.terrace_ = {};
		}

		if (const auto p = o.find("palette"); p != o.end())
		{
			onlyKeys(*p, { "top", "under", "rock", "sand", "snow" }, where + ".palette");
			BiomeDefinition::Palette& palette = biome.palette_;
			palette.top_ = text(*p, "top", palette.top_, where + ".palette");
			palette.under_ = text(*p, "under", palette.under_, where + ".palette");
			palette.rock_ = text(*p, "rock", palette.rock_, where + ".palette");
			palette.sand_ = text(*p, "sand", palette.sand_, where + ".palette");
			palette.snow_ = text(*p, "snow", palette.snow_, where + ".palette");
		}

		if (const auto s = o.find("strata"); s != o.end())
		{
			onlyKeys(*s, { "wobble", "bands" }, where + ".strata");
			biome.strataWobble_ = number<float>(*s, "wobble", biome.strataWobble_, where + ".strata");
			if (biome.strataWobble_ < 0.f)
				fail("\"wobble\" in " + where + ".strata must be >= 0");

			const auto b = s->find("bands");
			if (b == s->end() || !b->is_array())
				fail(where + ".strata needs a \"bands\" array");
			for (const json& entry : *b)
			{
				onlyKeys(entry, { "material", "thickness" }, where + ".strata.bands[]");
				BiomeDefinition::Band band;
				band.material_ = text(entry, "material", "", where + ".strata.bands[]");
				band.thickness_ = number<float>(entry, "thickness", band.thickness_, where + ".strata.bands[]");
				if (band.material_.empty())
					fail(where + ".strata.bands[] needs a \"material\"");
				positive(band.thickness_, "thickness", where + ".strata.bands[]");
				biome.strata_.push_back(std::move(band));
			}
			if (biome.strata_.empty())
				biome.strataWobble_ = BiomeDefinition{}.strataWobble_;
		}

		if (const auto r = o.find("rules"); r != o.end())
		{
			const std::string at = where + ".rules";
			onlyKeys(*r, { "soilDepth", "grassMaxSlopeDeg", "rockSlopeDeg", "snowline", "snowMaxSlopeDeg", "snowDepth",
						   "sandDeposit", "sandDepth" }, at);

			BiomeDefinition::Rules& rules = biome.rules_;
			rules.soilDepth_ = number<float>(*r, "soilDepth", rules.soilDepth_, at);
			rules.grassMaxSlopeDeg_ = number<float>(*r, "grassMaxSlopeDeg", rules.grassMaxSlopeDeg_, at);
			rules.rockSlopeDeg_ = number<float>(*r, "rockSlopeDeg", rules.rockSlopeDeg_, at);
			rules.snowline_ = number<float>(*r, "snowline", rules.snowline_, at);
			rules.snowMaxSlopeDeg_ = number<float>(*r, "snowMaxSlopeDeg", rules.snowMaxSlopeDeg_, at);
			rules.snowDepth_ = number<float>(*r, "snowDepth", rules.snowDepth_, at);
			rules.sandDeposit_ = number<float>(*r, "sandDeposit", rules.sandDeposit_, at);
			rules.sandDepth_ = number<float>(*r, "sandDepth", rules.sandDepth_, at);

			if (rules.soilDepth_ < 0.f || rules.snowDepth_ < 0.f || rules.sandDepth_ < 0.f)
				fail("depths in " + at + " must be >= 0");
			slopeDeg(rules.grassMaxSlopeDeg_, "grassMaxSlopeDeg", at);
			slopeDeg(rules.rockSlopeDeg_, "rockSlopeDeg", at);
			slopeDeg(rules.snowMaxSlopeDeg_, "snowMaxSlopeDeg", at);
			if (rules.rockSlopeDeg_ < rules.grassMaxSlopeDeg_)
				fail("\"rockSlopeDeg\" in " + at + " must not be below \"grassMaxSlopeDeg\"");
		}

		return biome;
	}

	/*!
		@brief
			Writes every field of a biome in a fixed key order, so the text is stable across saves.
		@param biome
			The biome to write
		@return
			The biome as an ordered JSON object
		*/
	ordered_json biomeToJson(const BiomeDefinition& biome)
	{
		ordered_json o;
		o["name"] = biome.name_;
		o["colour"] = { std::lround(biome.colour_.r * 255.f), std::lround(biome.colour_.g * 255.f), std::lround(biome.colour_.b * 255.f) };
		o["featureSize"] = tidy(biome.featureSize_);
		o["base"] = tidy(biome.base_);
		o["edgeRelief"] = tidy(biome.edgeRelief_);
		o["erodibility"] = tidy(biome.erodibility_);
		o["talusDeg"] = tidy(biome.talusDeg_);

		o["layers"] = ordered_json::array();
		for (const HeightLayer& layer : biome.layers_)
			o["layers"].push_back(layerToJson(layer));

		if (biome.terrace_.step_ > 0.f)
			o["terrace"] = { { "step", tidy(biome.terrace_.step_) }, { "sharpness", tidy(biome.terrace_.sharpness_) },
							 { "slack", tidy(biome.terrace_.slack_) } };

		o["palette"] = { { "top", biome.palette_.top_ }, { "under", biome.palette_.under_ }, { "rock", biome.palette_.rock_ },
						 { "sand", biome.palette_.sand_ }, { "snow", biome.palette_.snow_ } };

		if (!biome.strata_.empty())
		{
			ordered_json bands = ordered_json::array();
			for (const BiomeDefinition::Band& band : biome.strata_)
				bands.push_back({ { "material", band.material_ }, { "thickness", tidy(band.thickness_) } });
			o["strata"] = { { "wobble", tidy(biome.strataWobble_) }, { "bands", std::move(bands) } };
		}

		const BiomeDefinition::Rules& rules = biome.rules_;
		o["rules"] = { { "soilDepth", tidy(rules.soilDepth_) }, { "grassMaxSlopeDeg", tidy(rules.grassMaxSlopeDeg_) },
					   { "rockSlopeDeg", tidy(rules.rockSlopeDeg_) }, { "snowline", tidy(rules.snowline_) },
					   { "snowMaxSlopeDeg", tidy(rules.snowMaxSlopeDeg_) }, { "snowDepth", tidy(rules.snowDepth_) },
					   { "sandDeposit", tidy(rules.sandDeposit_) }, { "sandDepth", tidy(rules.sandDepth_) } };
		return o;
	}
}
