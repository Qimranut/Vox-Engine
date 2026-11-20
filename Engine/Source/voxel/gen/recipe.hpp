/*************************************************************************************************************
\file       recipe.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares Recipe, everything a terrain generator needs as parsed from a .voxgen file
            (version 2 landform or version 3 biome terrain, optionally endless), plus NoiseLayer.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Voxel
#include "biome/biome_definition.hpp"

// Noise
#include "../../util/noise/fbm.hpp"

// GLM
#include <glm/glm.hpp>

// stds
#include <cstdint>
#include <span>
#include <string>
#include <vector>


namespace voxel
{
	/**
	 * A fractal layer as a recipe writes it. Wavelength and amplitude are in
	 * metres, so a recipe survives a change of voxel size.
	 *
	 */
	struct NoiseLayer
	{
		double	wavelength_	{ 50.0 };
		double	amplitude_	{ 0.0 };
		int		octaves_	{ 4 };
		double	lacunarity_	{ 2.0 };
		double	gain_		{ 0.5 };

		bool operator==(const NoiseLayer&) const = default;

		/*!
			@brief
				Converts the layer to fbm parameters, dropping octaves too fine to keep.
			@param minWavelength
				Octaves finer than this are dropped: detail below a few grid cells
				is what turns into one-voxel spikes
			@return
				Parameters for inputs in metres
			*/
		util::noise::FbmParams fbm(double minWavelength) const;
	};

	/**
	 * Everything a terrain generator needs, parsed from a .voxgen file
	 * (version 2, or 3 for biome terrain). Heights and distances are in
	 * metres, angles in degrees. None of these numbers exist in code.
	 *
	 */
	struct Recipe
	{
		std::uint64_t	seed_		{ 0 };
		float			voxelSize_	{ 0.1f };
		glm::ivec3		size_		{ 1000, 512, 1000 };
		glm::vec3		origin_		{ 0.f };

		/** The uneroded landform. */
		struct Shape
		{
			float		baseHeight_			{ 8.f };

			NoiseLayer	warp_				{ 65.0, 9.0, 3, 2.0, 0.5 };
			NoiseLayer	massif_				{ 90.0, 28.0, 4, 2.0, 0.5 };
			float		massifFloor_		{ 0.35f };	// share of the amplitude every mountain gets, ridge or not
			NoiseLayer	detail_				{ 32.0, 6.0, 6, 1.98, 0.49 };
			float		detailDamping_		{ 0.35f };
			float		detailInterior_		{ 0.35f };	// detail amplitude where the ring mask is 0

			/**
			 * Mountains toward the map edge around an open interior. inner_
			 * and outer_ are on a rounded-square distance: 0 at the centre,
			 * 1 at the edge.
			 *
			 */
			struct Ring
			{
				bool	enabled_	{ false };
				float	inner_		{ 0.45f };
				float	outer_		{ 0.95f };
				float	power_		{ 1.4f };
				float	wobble_		{ 0.12f };

				bool operator==(const Ring&) const = default;
			} ring_;

			/** A bowl pressed into the landform; centre_ is 0..1 across the map. */
			struct Basin
			{
				glm::vec2	centre_	{ 0.5f };
				float		radius_	{ 15.f };
				float		depth_	{ 5.f };
				float		rim_	{ 0.f };

				bool operator==(const Basin&) const = default;
			};
			std::vector<Basin>	basins_;

			bool operator==(const Shape&) const = default;
		} shape_;

		/** Droplet hydraulic erosion, then thermal relaxation. */
		struct Erosion
		{
			int		cellVoxels_			{ 2 };
			int		droplets_			{ 300000 };
			int		radius_				{ 3 };
			float	inertia_			{ 0.08f };
			float	capacity_			{ 4.f };
			float	minCapacity_		{ 0.01f };
			float	erode_				{ 0.3f };
			float	deposit_			{ 0.3f };
			float	evaporate_			{ 0.012f };
			float	gravity_			{ 4.f };
			int		lifetime_			{ 48 };

			int		thermalIterations_	{ 60 };
			float	rockTalusDeg_		{ 52.f };
			float	sedimentTalusDeg_	{ 33.f };
			float	thermalRate_		{ 0.25f };

			// Relief in metres the droplet pass treats the map as having at least.
			// The pass works on heights scaled to 0..1, so without a floor a nearly
			// flat map is gullied as deeply, in proportion, as a mountain range.
			float	minRelief_			{ 0.f };

			bool operator==(const Erosion&) const = default;
		} erosion_;

		/** Lakes: depressions filled with a solid water material. */
		struct Water
		{
			bool		enabled_		{ false };
			std::string	material_		{ "water" };
			float		minDepth_		{ 0.4f };
			float		minArea_		{ 40.f };
			float		maxDepth_		{ 3.5f };
			float		fillFraction_	{ 0.85f };

			// Largest surface one lake may have, m^2; 0 is no limit. A lake over
			// the limit keeps to the deepest part of its basin. Biome terrain
			// needs it: a biome that sits lower than its neighbours is a basin
			// the size of a whole cell.
			float		maxArea_		{ 0.f };

			bool operator==(const Water&) const = default;
		} water_;

		/** Small-scale roughness added after the upsample, gated by slope. */
		struct DetailNoise
		{
			NoiseLayer	layer_		{ 1.6, 0.12, 3, 2.0, 0.5 };
			float		fullSlopeDeg_	{ 30.f };

			bool operator==(const DetailNoise&) const = default;
		} detailNoise_;

		/** Which material a column shows, and what lies under it. */
		struct Surface
		{
			std::string	top_				{ "grass" };
			std::string	under_				{ "dirt" };
			std::string	base_				{ "stone" };
			std::string	sand_				{ "sand" };
			std::string	snow_				{ "snow" };

			float		soilDepth_			{ 1.2f };
			float		grassMaxSlopeDeg_	{ 33.f };
			float		rockSlopeDeg_		{ 44.f };
			float		slopeJitterDeg_		{ 4.f };
			float		jitterWavelength_	{ 6.f };
			float		slopeReach_			{ 0.4f };

			float		snowline_			{ 30.f };
			float		snowJitter_			{ 2.5f };
			float		snowMaxSlopeDeg_	{ 40.f };
			float		snowDepth_			{ 0.3f };

			float		sandDeposit_		{ 0.15f };
			float		sandMaxHeight_		{ 10.f };
			float		shoreBand_			{ 0.5f };
			float		shoreReach_			{ 0.8f };
			float		sandDepth_			{ 0.6f };

			bool operator==(const Surface&) const = default;
		} surface_;

		struct Caves
		{
			bool					enabled_				{ false };
			util::noise::FbmParams	fbm_;
			double					threshold_				{ 0.58 };
			int						minDepthBelowSurface_	{ 30 };
			int						minY_					{ 8 };

			bool operator==(const Caves&) const = default;
		} caves_;

		/**
		 * Version 3: the landform comes from biomes laid out in cells instead
		 * of from shape_, and each biome brings its own materials and surface
		 * rules (see gen/biome/). The definitions are stored whole, so the
		 * recipe alone still decides the terrain; the biome library they were
		 * copied from can change without touching maps already made.
		 *
		 * Sampled in world space: a recipe with a different origin_ shows a
		 * different window of the same endless layout.
		 *
		 */
		struct Biomes
		{
			bool	enabled_		{ false };
			float	size_			{ 100.f };	// cell spacing, metres
			float	blending_		{ 0.3f };	// 0..1, width of the cross-fade as a share of size_
			float	datumHeight_	{ 15.f };	// height above the map's floor that biome heights are about
			float	reliefScale_	{ 1.f };	// shrinks every biome's heights to fit a low map
			float	shellDepth_		{ 8.f };	// how far below the surface a biome's own rock reaches

			std::vector<BiomeDefinition>	entries_;

			bool operator==(const Biomes&) const = default;
		} biomes_;

		/**
		 * A terrain too large to make in one piece: size_ is then the whole
		 * extent the player may roam (at most kMaxEndlessVoxels a side), and
		 * the terrain is generated around them as they go, in square tiles of
		 * tileCells_ x tileCells_ cells (a cell is 256 voxels, the World's
		 * page). Each tile is eroded on its own and faded back to the biomes'
		 * uneroded ground along its border, which is a function of position
		 * alone, so neighbours meet exactly; see gen/stream/tile_generator.hpp.
		 *
		 * Biome terrain only: a shaped landform (shape_) is laid out across the
		 * map as a whole and cannot be cut into tiles. Such a recipe is never
		 * generated whole - TerrainGenerator refuses - and never has a .voxmap.
		 *
		 */
		struct Stream
		{
			static constexpr int kMaxEndlessVoxels = 32768;		// what a section record can address; render/draw_types.hpp

			bool	endless_	{ false };
			int		tileCells_	{ 4 };		// 1, 2 or 4

			bool operator==(const Stream&) const = default;
		} stream_;

		/*!
			@brief
				Parses a .voxgen (version 2 or 3).

				Unknown keys are errors, missing optional blocks take the defaults
				above. Throws std::runtime_error.
			@param json
				The file's bytes
			@return
				The parsed recipe
			*/
		static Recipe parse(std::span<const unsigned char> json);

		/*!
			@brief
				The recipe as .voxgen text.

				parse(toJson()) == *this, and the text is stable: the same recipe
				always writes the same bytes.
			@return
				The JSON text
			*/
		std::string toJson() const;

		bool operator==(const Recipe&) const = default;
	};
}
