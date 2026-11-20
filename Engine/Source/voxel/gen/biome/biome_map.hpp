/*************************************************************************************************************
\file       biome_map.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      BiomeMap: an endless, seed-determined layout of jittered biome cells behind a domain warp,
            giving per-point biome weights that cross-fade along borders.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Noise
#include "../../../util/noise/fbm.hpp"

// stds
#include <array>
#include <cstddef>
#include <cstdint>


namespace voxel
{
	/** Biomes one map can mix; the weights of a sample are a fixed array of this size. */
	inline constexpr std::size_t kMaxBiomes = 16;

	/** What the biome map says about one point. */
	struct BiomeSample
	{
		std::array<float, kMaxBiomes>	weight_	{};			// sums to 1; 0 outside the blend band
		int								nearest_	{ 0 };	// biome of the cell the point lies in

		// Distance in metres to the nearest border with a different biome;
		// large when every cell in reach has the same one.
		double							edge_		{ 1e9 };
	};

	/**
	 * Which biome covers which ground: an endless field of cells about
	 * size metres across, each holding one of count biomes, with weights
	 * that cross-fade over a band along every border.
	 *
	 * Cells are the regions nearest to one site per square of a size-metre
	 * grid, each site pushed off its square's centre, and the point is
	 * domain-warped first so borders wander instead of running straight.
	 *
	 * A pure function of (seed, point): a point has the same biome and the
	 * same weights whatever map it is sampled for, which is what lets a
	 * terrain region be moved or resized over one consistent layout.
	 *
	 */
	class BiomeMap
	{
	public:

		/*!
			@brief
				Builds the layout for a seed; throws std::runtime_error for a bad count or size.
			@param seed
				Seed the site positions, warp and dealing pattern derive from
			@param count
				Biomes to deal out, 1..kMaxBiomes
			@param size
				Cell spacing in metres
			@param blending
				0..1: width of the cross-fade as a share of size. 0 still leaves
				a narrow band, so ground never meets at a cliff
			@param minBand
				Narrowest half-width of the band in metres; a few grid cells
			*/
		BiomeMap(std::uint64_t seed, int count, double size, double blending, double minBand);

		/*!
			@brief
				Samples the biome weights, nearest biome and border distance at a point.
			@param x
				World X position in metres
			@param z
				World Z position in metres
			@return
				The biome sample at that point
			*/
		BiomeSample sample(double x, double z) const;

		/*!
			@brief
				Gets the number of biomes dealt out.
			@return
				The biome count
			*/
		int count() const { return count_; }

		/*!
			@brief
				Gets the cell spacing.
			@return
				The cell spacing in metres
			*/
		double size() const { return size_; }

		/*!
			@brief
				Gets the half-width of the cross-fade band.
			@return
				The band half-width in metres
			*/
		double band() const { return band_; }

	private:

		std::uint64_t			seedSites_, seedWarp_;
		int						count_;
		double					size_, band_;
		int						deal_, stride_;	// per-seed offset into the dealing pattern, and its step per row
		util::noise::FbmParams	warpFbm_;
		double					warpAmplitude_;
	};
}
