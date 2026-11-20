/*************************************************************************************************************
\file       biome_json.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      A biome definition to and from JSON. The same object appears in the biome library and, copied,
            in a recipe's "biomes.entries". Internal to voxel/gen: include it from .cpp files only, it
            pulls in the JSON library.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Voxel
#include "biome_definition.hpp"

// JSON
#include <nlohmann/json.hpp>

// stds
#include <string>


namespace voxel
{
	/*!
		@brief
			Parses a biome definition from a JSON object.

			Unknown keys, unknown height ops and parameters an op does not take
			are errors. Throws std::runtime_error without a file prefix.
		@param object
			The JSON object holding the biome
		@param where
			How error messages name this object, e.g. "biomes[2]"
		@return
			The parsed biome definition
		*/
	BiomeDefinition biomeFromJson(const nlohmann::json& object, const std::string& where);

	/*!
		@brief
			Writes a biome definition as JSON.

			Every field, in a fixed key order, so the text is stable across saves.
		@param biome
			The biome to write
		@return
			The biome as an ordered JSON object
		*/
	nlohmann::ordered_json biomeToJson(const BiomeDefinition& biome);
}
