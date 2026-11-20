/*************************************************************************************************************
\file       biome_definition.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Plain data for a biome: its height layers (landform), terrace, erosion traits, material
            palette, rock strata and surface rules, as parsed from the biome library and embedded in recipes.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// GLM
#include <glm/glm.hpp>

// stds
#include <map>
#include <string>
#include <vector>


namespace voxel
{
	/**
	 * One term of a biome's landform: a named height op (see height_ops.hpp)
	 * sampled at a wavelength and scaled to an amplitude, both in metres.
	 *
	 * The fields every op shares live here. What only one op understands
	 * (a dune's wind direction, a plateau's thresholds) goes in params_,
	 * and the op's registry entry says which names it accepts - so a new op
	 * needs no change to this struct, the parser or the recipe format.
	 *
	 */
	struct HeightLayer
	{
		std::string	op_			{ "fbm" };
		double		wavelength_	{ 50.0 };
		double		amplitude_	{ 1.0 };
		int			octaves_	{ 4 };
		double		lacunarity_	{ 2.0 };
		double		gain_		{ 0.5 };

		/** Domain warp applied to the sample point first; amplitude 0 is none. */
		struct Warp
		{
			double	wavelength_	{ 40.0 };
			double	amplitude_	{ 0.0 };

			bool operator==(const Warp&) const = default;
		} warp_;

		/**
		 * Slow noise that scales the layer's amplitude between floor_ and 1,
		 * so a feature comes and goes across the biome. wavelength 0 is none.
		 *
		 */
		struct Mask
		{
			double	wavelength_	{ 0.0 };
			double	floor_		{ 0.0 };

			bool operator==(const Mask&) const = default;
		} mask_;

		std::map<std::string, double>	params_;

		bool operator==(const HeightLayer&) const = default;
	};

	/**
	 * Everything that makes a biome: its landform, how it erodes, and what
	 * it is made of. Heights are metres about the map's datum, angles are
	 * degrees. Parsed from the biome library (Assets/voxel/biomes.json) and
	 * copied whole into a recipe, so a recipe never depends on the library
	 * it was composed from.
	 *
	 */
	struct BiomeDefinition
	{
		std::string					name_;
		glm::vec3					colour_		{ 0.5f };	// sRGB 0..1, for previews and the Editor

		// The cell size (metres) this landform is drawn for. In smaller cells
		// it is shrunk in proportion, wavelengths and heights alike, so a
		// mountain range dealt a small cell becomes a small range rather than
		// one slope cut off at the border. 0 never shrinks.
		float						featureSize_	{ 0.f };

		float						base_		{ 0.f };	// mean ground level about the datum
		std::vector<HeightLayer>	layers_;				// summed into the relief

		/** Steps folded into the summed relief; step 0 is none. */
		struct Terrace
		{
			float	step_		{ 0.f };
			float	sharpness_	{ 6.f };
			float	slack_		{ 0.15f };	// share of the unfolded slope the treads keep, so they drain

			bool operator==(const Terrace&) const = default;
		} terrace_;

		// Share of the relief kept right at a border with another biome. Low
		// values make a mountain range sink to its base before the plains
		// start, instead of being cut off mid-slope.
		float						edgeRelief_	{ 1.f };

		float						erodibility_	{ 1.f };	// 0..1 share of the droplets' digging
		float						talusDeg_		{ 50.f };	// steepest slope bedrock holds

		/** Material names per cover role. */
		struct Palette
		{
			std::string	top_	{ "grass" };
			std::string	under_	{ "dirt" };
			std::string	rock_	{ "stone" };
			std::string	sand_	{ "sand" };
			std::string	snow_	{ "snow" };

			bool operator==(const Palette&) const = default;
		} palette_;

		/**
		 * Bands the bedrock near the surface is drawn in, bottom to top,
		 * repeating; empty means plain palette_.rock_. wobble_ is how far
		 * (metres) the bands rise and fall across the map.
		 *
		 */
		struct Band
		{
			std::string	material_;
			float		thickness_	{ 1.f };

			bool operator==(const Band&) const = default;
		};
		std::vector<Band>			strata_;
		float						strataWobble_	{ 1.f };

		/** The per-biome share of the surface rules (see surface_rules.hpp). */
		struct Rules
		{
			float	soilDepth_			{ 1.2f };
			float	grassMaxSlopeDeg_	{ 33.f };
			float	rockSlopeDeg_		{ 44.f };
			float	snowline_			{ 1000.f };	// metres above the datum; out of reach means never
			float	snowMaxSlopeDeg_	{ 40.f };
			float	snowDepth_			{ 0.3f };
			float	sandDeposit_		{ 0.15f };
			float	sandDepth_			{ 0.6f };

			bool operator==(const Rules&) const = default;
		} rules_;

		bool operator==(const BiomeDefinition&) const = default;
	};
}
