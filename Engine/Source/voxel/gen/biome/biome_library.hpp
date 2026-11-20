/*************************************************************************************************************
\file       biome_library.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      The biome library read from Assets/voxel/biomes.json, the user's generate settings and their
            limits, and composeBiomeRecipe(), which turns those choices into a version-3 recipe.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Voxel
#include "biome_definition.hpp"
#include "../recipe.hpp"

// GLM
#include <glm/glm.hpp>

// stds
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>


namespace voxel
{
	/**
	 * The biomes a terrain can be generated from, read from
	 * Assets/voxel/biomes.json, plus the recipe settings every generated
	 * terrain starts from (erosion, water, detail, surface).
	 *
	 * A preset library and nothing more: composeBiomeRecipe() copies what it
	 * needs into the recipe, and from then on the recipe stands alone. A new
	 * biome is a new entry in that file.
	 *
	 */
	class BiomeLibrary
	{
	public:

		/*!
			@brief
				Parses a biome library from the bytes of biomes.json.

				Unknown keys, unknown height ops and duplicate names are errors. Throws std::runtime_error.
			@param json
				The file's bytes
			@return
				The parsed library
			*/
		static BiomeLibrary parse(std::span<const unsigned char> json);

		/*!
			@brief
				Gets every biome in the library, in file order.
			@return
				The biome definitions
			*/
		const std::vector<BiomeDefinition>& biomes() const { return biomes_; }

		/*!
			@brief
				Looks a biome up by name.
			@param name
				The biome's name
			@return
				The biome, or nullptr when there is no biome of that name
			*/
		const BiomeDefinition* find(std::string_view name) const;

		/*!
			@brief
				Gets the recipe blocks a composed recipe starts from; its size, seed and biomes are placeholders.
			@return
				The default recipe
			*/
		const Recipe& defaults() const { return defaults_; }

		/*!
			@brief
				Gets how many erosion droplets a composed recipe gets per square metre.
			@return
				The droplet density
			*/
		float dropletsPerSquareMetre() const { return dropletsPerSquareMetre_; }

	private:

		std::vector<BiomeDefinition>	biomes_;
		Recipe							defaults_;
		float							dropletsPerSquareMetre_	{ 40.f };
	};

	/** What the user chooses when generating terrain. */
	struct GenerateSettings
	{
		glm::ivec3					size_		{ 1000, 384, 1000 };	// voxels
		glm::vec3					origin_		{ 0.f };				// world-space min corner, metres
		float						voxelSize_	{ 0.1f };
		std::vector<std::string>	biomes_;							// library names, any order
		float						blending_	{ 0.3f };				// 0..1
		float						biomeSize_	{ 40.f };				// metres
		std::uint64_t				seed_		{ 0 };

		// A streamed terrain (Recipe::Stream): size_.x and size_.z are then the
		// whole extent the player may roam, rounded up to whole tiles, and the
		// horizontal limits below do not apply - at most
		// Recipe::Stream::kMaxEndlessVoxels a side instead.
		bool						endless_	{ false };
		int							tileCells_	{ 4 };
	};

	/** The sizes composeBiomeRecipe() accepts, in voxels. */
	struct GenerateLimits
	{
		static constexpr int			kMinHorizontal	= 32;
		static constexpr int			kMaxHorizontal	= 2048;
		static constexpr int			kMinVertical	= 128;
		static constexpr int			kMaxVertical	= 1024;
		static constexpr std::int64_t	kMaxColumns		= 4200000;		// x * z
		static constexpr float			kMinBiomeSize	= 10.f;			// metres
		static constexpr float			kMaxBiomeSize	= 1000.f;
	};

	/*!
		@brief
			Turns the user's choices into a complete version-3 recipe.

			The chosen biomes go in library order, the erosion effort is scaled
			to the area, and the datum and relief scale fit the landforms inside
			the height available. The one place those decisions are made, shared
			by the Editor, VoxelProbe and the self-check.

			The origin is snapped to whole voxels, because biome terrain is laid
			out on the world's voxel lattice.

			Throws std::runtime_error for an unknown biome, no biome at all, or a
			size outside GenerateLimits.
		@param library
			The biome library to take definitions and defaults from
		@param settings
			The user's choices
		@return
			The composed recipe
		*/
	Recipe composeBiomeRecipe(const BiomeLibrary& library, const GenerateSettings& settings);
}
