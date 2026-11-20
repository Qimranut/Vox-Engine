/*************************************************************************************************************
\file       biome_map.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      BiomeMap implementation: jittered-grid sites, lattice biome dealing, domain warp and the
            compact kernel that weights each biome by its nearest site.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "biome_map.hpp"

// Noise
#include "../../../util/noise/hash.hpp"

// stds
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <string>


namespace voxel
{
	namespace
	{
		using namespace util::noise;

		// A site sits within this share of a square's width of its centre.
		// Small enough that the 5x5 squares around a point always hold every
		// site that can reach it (see sample()).
		constexpr double kJitter = 0.25;

		// Border wander, as shares of the cell size.
		constexpr double kWarpAmplitude = 0.22;
		constexpr double kWarpWavelength = 1.3;

		// The narrowest band, as a share of the cell size and capped in metres:
		// wide enough that two landforms never meet at a wall.
		constexpr double kMinBandShare = 0.06;
		constexpr double kMinBandCap = 6.0;

		constexpr double kToUnit = 1.0 / 4294967296.0;		// 2^-32

		// Cells dealt by hash rather than by the lattice, out of 65536.
		constexpr std::uint64_t kWildShare = 13107;

		/*!
			@brief
				Compact bump: 1 at t = 0, 0 from t = 1 on, smooth at both ends.
			@param t
				Distance as a share of the kernel's reach
			@return
				The weight, 0..1
			*/
		inline double kernel(double t)
		{
			if (t >= 1.0)
				return 0.0;
			const double k = 1.0 - t * t;
			return k * k * k;
		}
	}

	/*!
		@brief
			Builds the layout: derives seeds, the band width, the dealing offset and a stride coprime to count.
		@param seed
			Seed the site positions, warp and dealing pattern derive from
		@param count
			Biomes to deal out, 1..kMaxBiomes
		@param size
			Cell spacing in metres
		@param blending
			0..1: width of the cross-fade as a share of size
		@param minBand
			Narrowest half-width of the band in metres
		*/
	BiomeMap::BiomeMap(std::uint64_t seed, int count, double size, double blending, double minBand)
		: seedSites_(seed ^ hashString("biomes.sites"))
		, seedWarp_(seed ^ hashString("biomes.warp"))
		, count_(count)
		, size_(size)
	{
		if (count < 1 || count > static_cast<int>(kMaxBiomes))
			throw std::runtime_error("a biome map mixes 1.." + std::to_string(kMaxBiomes) + " biomes");
		if (!(size > 0.0))
			throw std::runtime_error("biome size must be positive");

		const double floor = std::max(minBand, std::min(kMinBandShare * size, kMinBandCap));
		band_ = std::max(std::clamp(blending, 0.0, 1.0) * 0.5 * size, floor);

		// 2 * band_ is the reach of a site's weight past the nearest one; the
		// 5x5 search is only complete while that stays within one cell.
		band_ = std::min(band_, 0.5 * size);

		deal_ = static_cast<int>(hash64(seed ^ hashString("biomes.deal")) % static_cast<std::uint64_t>(count));

		// The stride nearest the golden share of count that is coprime to it,
		// so the pattern visits every biome and spreads repeats apart.
		stride_ = 1;
		const int wanted = std::max(1, static_cast<int>(std::lround(0.618 * count)));
		for (int away = 0; away < count; ++away)
		{
			const int candidates[2] = { wanted + away, wanted - away };
			bool found = false;
			for (const int c : candidates)
				if (c >= 1 && c < std::max(2, count) && std::gcd(c, count) == 1) { stride_ = c; found = true; break; }
			if (found)
				break;
		}

		warpAmplitude_ = kWarpAmplitude * size;
		warpFbm_.frequency_ = 1.0 / (kWarpWavelength * size);
		warpFbm_.octaves_ = 2;
	}

	/*!
		@brief
			Samples the biome weights, nearest biome and border distance at a point.

			Warps the point, searches the 5x5 squares of sites around it, and
			weights each biome by its closest site's distance past the nearest.
		@param x
			World X position in metres
		@param z
			World Z position in metres
		@return
			The biome sample at that point
		*/
	BiomeSample BiomeMap::sample(double x, double z) const
	{
		BiomeSample out;
		if (count_ == 1)
		{
			out.weight_[0] = 1.f;
			return out;
		}

		double qx, qz;
		domainWarp2(seedWarp_, x, z, warpAmplitude_, warpFbm_, qx, qz);

		const double ux = qx / size_, uz = qz / size_;
		const std::int64_t cx = static_cast<std::int64_t>(std::floor(ux));
		const std::int64_t cz = static_cast<std::int64_t>(std::floor(uz));

		// The nearest site is at most ~1.06 cells away (the far corner of the
		// point's own square) and one outside the 5x5 block is at least 2.25,
		// so everything within a cell's reach of the nearest is in the block.
		constexpr int kReach = 2;
		constexpr int kSites = (2 * kReach + 1) * (2 * kReach + 1);

		double distance[kSites];
		int biome[kSites];
		double nearest = 1e300;
		int nearestSite = 0;

		int s = 0;
		for (int dz = -kReach; dz <= kReach; ++dz)
		for (int dx = -kReach; dx <= kReach; ++dx, ++s)
		{
			const std::int64_t sx = cx + dx, sz = cz + dz;
			const std::uint64_t h = hashCoords(seedSites_, sx, sz);
			const double jx = (static_cast<double>(h >> 32) * kToUnit - 0.5) * 2.0 * kJitter;
			const double jz = (static_cast<double>(h & 0xFFFFFFFFull) * kToUnit - 0.5) * 2.0 * kJitter;

			const double px = (static_cast<double>(sx) + 0.5 + jx - ux) * size_;
			const double pz = (static_cast<double>(sz) + 0.5 + jz - uz) * size_;
			distance[s] = std::sqrt(px * px + pz * pz);

			// Biomes are dealt in a lattice pattern, (x + stride * z) mod count:
			// every run of count cells holds every biome and neighbours differ,
			// so even a map only a few cells wide shows all that were chosen. A
			// plain hash promises neither. One cell in five is dealt by hash
			// instead, which breaks up the pattern's diagonals on large maps.
			const std::uint64_t pick = hash64(h);
			if ((pick & 0xFFFFull) < kWildShare)
				biome[s] = static_cast<int>((pick >> 16) % static_cast<std::uint64_t>(count_));
			else
			{
				const std::int64_t dealt = (sx + static_cast<std::int64_t>(stride_) * sz + deal_) % count_;
				biome[s] = static_cast<int>(dealt < 0 ? dealt + count_ : dealt);
			}

			if (distance[s] < nearest)
			{
				nearest = distance[s];
				nearestSite = s;
			}
		}

		out.nearest_ = biome[nearestSite];

		// Each biome weighs as much as its closest site, measured by how much
		// further away than the nearest site it is. The nearest site's weight
		// is 1, so the sum never vanishes.
		const double reach = 2.0 * band_;
		double total = 0.0;
		double weight[kMaxBiomes] = {};

		for (s = 0; s < kSites; ++s)
		{
			const double past = distance[s] - nearest;
			if (biome[s] != out.nearest_)
				out.edge_ = std::min(out.edge_, 0.5 * past);

			const double w = kernel(past / reach);
			if (w > weight[biome[s]])
				weight[biome[s]] = w;
		}

		for (int b = 0; b < count_; ++b)
			total += weight[b];
		for (int b = 0; b < count_; ++b)
			out.weight_[static_cast<std::size_t>(b)] = static_cast<float>(weight[b] / total);

		return out;
	}
}
