/*************************************************************************************************************
\file       material_table.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      The voxel material registry: voxel::Material (one entry of Assets/voxel/materials.json) and
            voxel::MaterialTable, which parses the file, maps names to runtime ids and hashes itself.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Voxel
#include "voxel_types.hpp"

// GLM
#include <glm/glm.hpp>

// stds
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>


namespace voxel
{
	/**
	 * One entry of Assets/voxel/materials.json. Everything gameplay or
	 * rendering knows about a voxel type lives here, never in code.
	 *
	 */
	struct Material
	{
		std::string		name_;
		glm::vec3		colourSrgb_		{ 1.f };	// as authored, 0..1
		glm::vec3		colourLinear_	{ 1.f };	// what the shader multiplies
		std::uint8_t	lodPriority_	{ 0 };		// higher wins when 8 voxels collapse into one
		float			friction_		{ 0.6f };
		bool			opaque_			{ true };	// only air is transparent for now
		std::string		texture_;					// optional albedo path, unused by the renderer yet
		std::string		yields_;					// material the extraction attack yields; empty for none
	};

	/**
	 * The material registry. Index 0 is always "air". Ids are runtime
	 * only: files store names and remap on load.
	 *
	 */
	class MaterialTable
	{
	public:

		MaterialTable() = default;

		/*!
			@brief
				Parses materials.json.
			@param json
				File bytes
			@return
				The table; throws std::runtime_error naming the offending key
			*/
		static MaterialTable parse(std::span<const unsigned char> json);

		/*!
			@brief
				A built-in table matching the shipped materials.json.

				Air, stone, dirt, grass, sand, snow, water and the biome materials,
				for self-checks and for running without the asset.
			@return
				The table
			*/
		static MaterialTable builtin();

		/*!
			@brief
				Appends a material; validates the air rule and uniqueness.
			@param material
				The material to add
			@return
				Its new id
			*/
		MaterialId add(Material material);

		/*!
			@brief
				Looks a material up by name.
			@param name
				The material's name
			@return
				The id, or kUnknownMaterial
			*/
		MaterialId id(std::string_view name) const;

		/*!
			@brief
				The material with an id.
			@param id
				The material id
			@return
				The material, or a magenta placeholder for an unknown id
			*/
		const Material& get(MaterialId id) const;

		/*!
			@brief
				The number of materials, air included.
			@return
				The count
			*/
		std::size_t size() const { return materials_.size(); }

		/*!
			@brief
				Every material, in id order.
			@return
				The materials
			*/
		const std::vector<Material>& materials() const { return materials_; }

		/*!
			@brief
				Whether a material is opaque.
			@param id
				The material id
			@return
				false for air, non-opaque or unknown ids
			*/
		bool opaque(MaterialId id) const { return id < materials_.size() && materials_[id].opaque_; }

		/*!
			@brief
				Hash of names and LoD priorities.

				What a cooked map records so a loader can tell the table changed
				underneath it.
			@return
				The hash
			*/
		std::uint64_t hash() const;

	private:

		std::vector<Material>							materials_;
		std::unordered_map<std::string, MaterialId>		byName_;
	};
}
