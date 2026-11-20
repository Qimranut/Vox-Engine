/*************************************************************************************************************
\file       material_table.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      MaterialTable: materials.json parsing and validation, the built-in fallback table, name lookup
            and the table hash a cooked map records.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "material_table.hpp"

// Noise (hashing)
#include "../../util/noise/hash.hpp"

// JSON
#include <nlohmann/json.hpp>

// stds
#include <cmath>
#include <stdexcept>


namespace voxel
{
	namespace
	{
		/*!
			@brief
				Decodes one sRGB channel to linear.
			@param c
				sRGB value, 0..1
			@return
				Linear value
			*/
		float srgbToLinear(float c)
		{
			return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
		}

		/*!
			@brief
				Decodes an sRGB colour to linear, per channel.
			@param c
				sRGB colour, 0..1
			@return
				Linear colour
			*/
		glm::vec3 srgbToLinear(const glm::vec3& c)
		{
			return { srgbToLinear(c.x), srgbToLinear(c.y), srgbToLinear(c.z) };
		}

		/*!
			@brief
				Throws std::runtime_error if the object has a key not in the allowed list.
			@param object
				The JSON object to check
			@param allowed
				The permitted keys
			@param where
				What the error message calls the object
			*/
		void requireKeys(const nlohmann::json& object, std::initializer_list<const char*> allowed, const std::string& where)
		{
			for (const auto& [key, value] : object.items())
			{
				bool ok = false;
				for (const char* a : allowed)
					if (key == a) { ok = true; break; }

				if (!ok)
					throw std::runtime_error("materials.json: unknown key \"" + key + "\" in " + where);
			}
		}
	}

	/*!
		@brief
			Parses materials.json (version 1) and checks every yields target exists.
		@param json
			File bytes
		@return
			The table; throws std::runtime_error naming the offending key
		*/
	MaterialTable MaterialTable::parse(std::span<const unsigned char> json)
	{
		nlohmann::json doc;
		try
		{
			doc = nlohmann::json::parse(json.begin(), json.end());
		}
		catch (const nlohmann::json::exception& e)
		{
			throw std::runtime_error(std::string("materials.json: ") + e.what());
		}

		if (!doc.is_object())
			throw std::runtime_error("materials.json: root must be an object");

		requireKeys(doc, { "version", "materials" }, "root");

		if (doc.value("version", 0) != 1)
			throw std::runtime_error("materials.json: unsupported version");

		const auto it = doc.find("materials");
		if (it == doc.end() || !it->is_array())
			throw std::runtime_error("materials.json: \"materials\" must be an array");

		MaterialTable table;

		for (const nlohmann::json& m : *it)
		{
			if (!m.is_object())
				throw std::runtime_error("materials.json: each material must be an object");

			const std::string name = m.value("name", std::string{});
			requireKeys(m, { "name", "colour", "lodPriority", "friction", "opaque", "texture", "yields" }, "material \"" + name + "\"");

			if (name.empty())
				throw std::runtime_error("materials.json: material without a name");

			Material mat;
			mat.name_ = name;

			if (const auto c = m.find("colour"); c != m.end())
			{
				if (!c->is_array() || c->size() != 3)
					throw std::runtime_error("materials.json: \"colour\" of \"" + name + "\" must be [r, g, b] in 0..255");

				mat.colourSrgb_ = glm::vec3{ (*c)[0].get<float>(), (*c)[1].get<float>(), (*c)[2].get<float>() } / 255.f;
				mat.colourSrgb_ = glm::clamp(mat.colourSrgb_, 0.f, 1.f);
			}
			mat.colourLinear_ = srgbToLinear(mat.colourSrgb_);

			const int priority = m.value("lodPriority", 0);
			if (priority < 0 || priority > 255)
				throw std::runtime_error("materials.json: \"lodPriority\" of \"" + name + "\" must be 0..255");
			mat.lodPriority_ = static_cast<std::uint8_t>(priority);

			mat.friction_ = m.value("friction", 0.6f);
			mat.opaque_ = m.value("opaque", true);
			mat.texture_ = m.value("texture", std::string{});
			mat.yields_ = m.value("yields", std::string{});

			table.add(std::move(mat));
		}

		if (table.size() == 0)
			throw std::runtime_error("materials.json: no materials");

		// Every yields target must exist.
		for (const Material& mat : table.materials_)
		{
			if (!mat.yields_.empty() && table.id(mat.yields_) == kUnknownMaterial)
				throw std::runtime_error("materials.json: \"" + mat.name_ + "\" yields unknown material \"" + mat.yields_ + "\"");
		}

		return table;
	}

	/*!
		@brief
			A built-in table matching the shipped materials.json.
		@return
			The table
		*/
	MaterialTable MaterialTable::builtin()
	{
		MaterialTable t;

		Material air;
		air.name_ = "air";
		air.opaque_ = false;
		air.colourSrgb_ = air.colourLinear_ = glm::vec3{ 0.f };
		air.friction_ = 0.f;
		t.add(std::move(air));

		const auto solid = [&t](const char* name, glm::vec3 rgb255, std::uint8_t priority, float friction)
		{
			Material m;
			m.name_ = name;
			m.colourSrgb_ = rgb255 / 255.f;
			m.colourLinear_ = srgbToLinear(m.colourSrgb_);
			m.lodPriority_ = priority;
			m.friction_ = friction;
			t.add(std::move(m));
		};

		// Surface covers outrank what lies under them, so a distant LoD
		// keeps the colour of the ground rather than of the rock below it.
		solid("stone", { 128.f, 128.f, 132.f }, 1, 0.7f);
		solid("dirt",  { 121.f,  85.f,  58.f }, 2, 0.6f);
		solid("grass", {  86.f, 152.f,  61.f }, 3, 0.6f);
		solid("sand",  { 214.f, 196.f, 140.f }, 4, 0.5f);
		solid("snow",  { 238.f, 242.f, 246.f }, 6, 0.35f);
		solid("water", {  64.f, 140.f, 200.f }, 5, 0.4f);

		// Biome materials, in the order materials.json lists them.
		solid("ice",       { 176.f, 212.f, 232.f }, 5, 0.1f);
		solid("sandstone", { 201.f, 152.f,  98.f }, 2, 0.65f);
		solid("redrock",   { 158.f,  84.f,  58.f }, 1, 0.7f);
		solid("mud",       {  92.f,  74.f,  52.f }, 3, 0.45f);

		return t;
	}

	/*!
		@brief
			Appends a material; throws unless the first is non-opaque "air", names
			are unique and there are at most 255.
		@param material
			The material to add
		@return
			Its new id
		*/
	MaterialId MaterialTable::add(Material material)
	{
		if (materials_.size() >= 255)
			throw std::runtime_error("materials.json: at most 255 materials");

		if (materials_.empty())
		{
			if (material.name_ != "air" || material.opaque_)
				throw std::runtime_error("materials.json: the first material must be \"air\" with opaque = false");
		}

		if (byName_.contains(material.name_))
			throw std::runtime_error("materials.json: duplicate material \"" + material.name_ + "\"");

		const MaterialId id = static_cast<MaterialId>(materials_.size());
		byName_.emplace(material.name_, id);
		materials_.push_back(std::move(material));
		return id;
	}

	/*!
		@brief
			Looks a material up by name.
		@param name
			The material's name
		@return
			The id, or kUnknownMaterial
		*/
	MaterialId MaterialTable::id(std::string_view name) const
	{
		const auto it = byName_.find(std::string(name));
		return it == byName_.end() ? kUnknownMaterial : it->second;
	}

	/*!
		@brief
			The material with an id.
		@param id
			The material id
		@return
			The material, or a magenta placeholder for an unknown id
		*/
	const Material& MaterialTable::get(MaterialId id) const
	{
		static const Material kMissing{ "?", glm::vec3{ 1.f, 0.f, 1.f }, glm::vec3{ 1.f, 0.f, 1.f }, 0, 0.6f, true, {}, {} };
		return id < materials_.size() ? materials_[id] : kMissing;
	}

	/*!
		@brief
			Hashes every material's name, LoD priority and opacity, in order.
		@return
			The hash
		*/
	std::uint64_t MaterialTable::hash() const
	{
		std::uint64_t h = 0x9E3779B97F4A7C15ull;
		for (const Material& m : materials_)
		{
			h ^= util::noise::hashString(m.name_);
			h = util::noise::hash64(h + m.lodPriority_ + (m.opaque_ ? 0x100u : 0u));
		}
		return h;
	}
}
