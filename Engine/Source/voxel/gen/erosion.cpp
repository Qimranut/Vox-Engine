/*************************************************************************************************************
\file       erosion.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Implements droplet hydraulic erosion and talus-angle thermal erosion over a HeightField.
            Compiled optimised even in Debug, since the droplet loop dominates a generate.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

// Optimised in Debug builds too: the droplet loop is the longest stretch of
// a generate, and the Erode brush runs it on every stamp. The result does
// not depend on it - Debug and Release already generate the same terrain, and
// the content hash VoxelProbe prints is there to catch the day they do not.
#if defined(_MSC_VER) && defined(_DEBUG)
#pragma runtime_checks("", off)
#pragma optimize("gt", on)
#endif

#include "erosion.hpp"

// Noise
#include "../../util/noise/prng.hpp"

// GLM
#include <glm/glm.hpp>

// Profiling
#include "../../profile/profiler.hpp"

// stds
#include <algorithm>
#include <cmath>
#include <vector>


namespace voxel
{
	namespace
	{
		// Sediment deeper than this (metres) makes a cell rest at the loose angle.
		constexpr float kLooseSediment = 0.02f;

		struct BrushTap
		{
			int		dx_, dz_;
			float	weight_;
		};

		/*!
			@brief
				Cone weights over a disc, summing to 1.
			@param radius
				Disc radius in grid cells
			@return
				The taps with a positive weight
			*/
		std::vector<BrushTap> makeBrush(int radius)
		{
			std::vector<BrushTap> taps;
			float total = 0.f;

			for (int dz = -radius; dz <= radius; ++dz)
			for (int dx = -radius; dx <= radius; ++dx)
			{
				const float dist = std::sqrt(static_cast<float>(dx * dx + dz * dz));
				const float w = 1.f - dist / static_cast<float>(radius);
				if (w <= 0.f)
					continue;
				taps.push_back({ dx, dz, w });
				total += w;
			}

			for (BrushTap& t : taps)
				t.weight_ /= total;
			return taps;
		}

		struct Sample
		{
			float	height_, gradX_, gradZ_;
		};

		/*!
			@brief
				Bilinear height and gradient; (x, z) must lie inside [0, w-1) x [0, d-1).
			@param map
				Row-major height grid
			@param width
				Grid width in cells
			@param x
				Sample position along x, in cells
			@param z
				Sample position along z, in cells
			@return
				Interpolated height and its x / z gradient
			*/
		inline Sample sampleAt(const float* map, int width, float x, float z)
		{
			const int ix = static_cast<int>(x), iz = static_cast<int>(z);
			const float u = x - static_cast<float>(ix), v = z - static_cast<float>(iz);

			const float* row0 = map + static_cast<std::size_t>(iz) * static_cast<std::size_t>(width) + ix;
			const float* row1 = row0 + width;
			const float nw = row0[0], ne = row0[1], sw = row1[0], se = row1[1];

			Sample s;
			s.gradX_ = (ne - nw) * (1.f - v) + (se - sw) * v;
			s.gradZ_ = (sw - nw) * (1.f - u) + (se - ne) * u;
			s.height_ = nw * (1.f - u) * (1.f - v) + ne * u * (1.f - v) + sw * (1.f - u) * v + se * u * v;
			return s;
		}
	}

	/*!
		@brief
			Progress ticks erodeHydraulic will report for these parameters.
		@param params
			Erosion parameters from the recipe
		@return
			The droplet count divided by kDropletBatch, rounded up
		*/
	int hydraulicTicks(const Recipe::Erosion& params)
	{
		return (params.droplets_ + kDropletBatch - 1) / kDropletBatch;
	}

	/*!
		@brief
			Runs droplet hydraulic erosion on a normalised copy of the field, then writes it back.
		@param field
			Eroded in place
		@param sediment
			Receives the net material gained (+) or lost (-) per cell in metres
		@param params
			Erosion parameters from the recipe
		@param seed
			Seed for the droplet start positions
		@param progress
			Incremented once per kDropletBatch droplets, if given
		@param cancel
			Polled every kDropletBatch droplets; stops the pass when set
		@param erodibility
			Per-cell share of digging a droplet keeps; null means 1 everywhere
		@return
			false if cancelled part-way
		*/
	bool erodeHydraulic(HeightField& field, HeightField& sediment, const Recipe::Erosion& params, std::uint64_t seed,
						std::atomic<int>* progress, const std::atomic<bool>* cancel, const HeightField* erodibility)
	{
		VOX_PROFILE_FUNCTION_C(profile::colour::kVoxel);

		const int width = field.width_, depth = field.depth_;
		if (params.droplets_ <= 0 || width < 4 || depth < 4)
			return true;

		// The published parameter sets assume heights in [0, 1] on a unit
		// grid, so the pass runs on a normalised copy.
		const auto [lowIt, highIt] = std::minmax_element(field.h_.begin(), field.h_.end());
		const float low = *lowIt;
		const float relief = std::max({ *highIt - low, params.minRelief_, 1e-3f });

		// Where given, the share of its digging a droplet gets to keep: soft
		// ground everywhere would wash a dune field flat.
		const float* hardness = erodibility && erodibility->size() == field.size() ? erodibility->h_.data() : nullptr;

		std::vector<float> normalised(field.size());
		for (std::size_t i = 0; i < normalised.size(); ++i)
			normalised[i] = (field.h_[i] - low) / relief;

		float* map = normalised.data();
		const std::vector<BrushTap> brush = makeBrush(params.radius_);
		const float maxX = static_cast<float>(width - 1), maxZ = static_cast<float>(depth - 1);

		for (int droplet = 0; droplet < params.droplets_; ++droplet)
		{
			if (droplet % kDropletBatch == 0)
			{
				if (cancel && cancel->load(std::memory_order_relaxed))
					return false;
				if (progress && droplet != 0)
					progress->fetch_add(1, std::memory_order_relaxed);
			}

			util::noise::Pcg32 rng(seed, static_cast<std::uint64_t>(droplet));
			float x = rng.nextFloat() * (maxX - 1.f);
			float z = rng.nextFloat() * (maxZ - 1.f);
			float dirX = 0.f, dirZ = 0.f;
			float speed = 1.f, water = 1.f, load = 0.f;

			for (int life = 0; life < params.lifetime_; ++life)
			{
				const int nodeX = static_cast<int>(x), nodeZ = static_cast<int>(z);
				const float u = x - static_cast<float>(nodeX), v = z - static_cast<float>(nodeZ);
				const Sample here = sampleAt(map, width, x, z);

				dirX = dirX * params.inertia_ - here.gradX_ * (1.f - params.inertia_);
				dirZ = dirZ * params.inertia_ - here.gradZ_ * (1.f - params.inertia_);
				const float len = std::sqrt(dirX * dirX + dirZ * dirZ);
				if (len < 1e-12f)
					break;
				dirX /= len;
				dirZ /= len;

				x += dirX;
				z += dirZ;
				if (x < 0.f || z < 0.f || x >= maxX || z >= maxZ)
					break;

				const float deltaH = sampleAt(map, width, x, z).height_ - here.height_;
				const float capacity = std::max(-deltaH * speed * water * params.capacity_, params.minCapacity_);
				float* node = map + static_cast<std::size_t>(nodeZ) * static_cast<std::size_t>(width) + nodeX;

				if (load > capacity || deltaH > 0.f)
				{
					// Uphill: fill the pit behind us, but no higher than the
					// lip. Otherwise shed a share of the excess.
					const float amount = deltaH > 0.f ? std::min(deltaH, load) : (load - capacity) * params.deposit_;
					load -= amount;

					node[0] += amount * (1.f - u) * (1.f - v);
					node[1] += amount * u * (1.f - v);
					node[width] += amount * (1.f - u) * v;
					node[width + 1] += amount * u * v;
				}
				else
				{
					// Never dig deeper than the drop to the next point, or
					// the droplet bores a hole it cannot leave.
					const float amount = std::min((capacity - load) * params.erode_, -deltaH);

					for (const BrushTap& tap : brush)
					{
						const int bx = nodeX + tap.dx_, bz = nodeZ + tap.dz_;
						if (bx < 0 || bz < 0 || bx >= width || bz >= depth)
							continue;

						const std::size_t at = static_cast<std::size_t>(bz) * static_cast<std::size_t>(width) + bx;
						float& cell = map[at];
						const float dug = std::min(cell, hardness ? amount * tap.weight_ * hardness[at] : amount * tap.weight_);
						cell -= dug;
						load += dug;
					}
				}

				speed = std::sqrt(std::max(0.f, speed * speed - deltaH * params.gravity_));
				water *= 1.f - params.evaporate_;
			}
		}

		if (progress)
			progress->fetch_add(1, std::memory_order_relaxed);

		for (std::size_t i = 0; i < normalised.size(); ++i)
		{
			const float after = normalised[i] * relief + low;
			sediment.h_[i] += after - field.h_[i];
			field.h_[i] = after;
		}

		return true;
	}

	/*!
		@brief
			Runs a fixed number of Jacobi thermal-erosion passes, moving material above the talus angle downhill.
		@param field
			Eroded in place
		@param sediment
			Picks the loose or rock talus per cell and follows the material that moves
		@param params
			Erosion parameters from the recipe
		@param cancel
			Polled once per pass; stops the pass when set
		@param rockTalus
			Per-cell tangent of the bedrock talus; null means params.rockTalusDeg_ everywhere
		@return
			false if cancelled part-way
		*/
	bool erodeThermal(HeightField& field, HeightField& sediment, const Recipe::Erosion& params,
					  const std::atomic<bool>* cancel, const HeightField* rockTalus)
	{
		VOX_PROFILE_FUNCTION_C(profile::colour::kVoxel);

		const int width = field.width_, depth = field.depth_;
		if (params.thermalIterations_ <= 0 || width < 2 || depth < 2)
			return true;

		constexpr int kDx[8] = { -1, 1, 0, 0, -1, 1, -1, 1 };
		constexpr int kDz[8] = { 0, 0, -1, 1, -1, -1, 1, 1 };
		const float straight = field.cell_, diagonal = field.cell_ * 1.41421356f;
		const float run[8] = { straight, straight, straight, straight, diagonal, diagonal, diagonal, diagonal };

		const float rockTan = std::tan(glm::radians(params.rockTalusDeg_));
		const float looseTan = std::tan(glm::radians(params.sedimentTalusDeg_));

		// Where given, the tangent bedrock holds per cell: canyon walls stand
		// where a hillside would slump.
		const float* rockTanAt = rockTalus && rockTalus->size() == field.size() ? rockTalus->h_.data() : nullptr;

		std::vector<float> delta(field.size());
		float* h = field.h_.data();
		float* loose = sediment.h_.data();

		for (int pass = 0; pass < params.thermalIterations_; ++pass)
		{
			if (cancel && cancel->load(std::memory_order_relaxed))
				return false;

			std::fill(delta.begin(), delta.end(), 0.f);

			for (int z = 0; z < depth; ++z)
			for (int x = 0; x < width; ++x)
			{
				const std::size_t i = static_cast<std::size_t>(z) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
				const float rock = rockTanAt ? rockTanAt[i] : rockTan;
				const float talus = loose[i] > kLooseSediment ? std::min(looseTan, rock) : rock;

				float excess[8];
				float total = 0.f, steepest = 0.f;

				for (int n = 0; n < 8; ++n)
				{
					excess[n] = 0.f;
					const int nx = x + kDx[n], nz = z + kDz[n];
					if (nx < 0 || nz < 0 || nx >= width || nz >= depth)
						continue;

					const std::size_t j = static_cast<std::size_t>(nz) * static_cast<std::size_t>(width) + static_cast<std::size_t>(nx);
					const float over = (h[i] - h[j]) - talus * run[n];
					if (over > 0.f)
					{
						excess[n] = over;
						total += over;
						steepest = std::max(steepest, over);
					}
				}

				if (total <= 0.f)
					continue;

				const float move = params.thermalRate_ * steepest;
				delta[i] -= move;

				for (int n = 0; n < 8; ++n)
				{
					if (excess[n] <= 0.f)
						continue;
					const std::size_t j = static_cast<std::size_t>(z + kDz[n]) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x + kDx[n]);
					delta[j] += move * (excess[n] / total);
				}
			}

			for (std::size_t i = 0; i < delta.size(); ++i)
			{
				h[i] += delta[i];
				loose[i] += delta[i];
			}
		}

		return true;
	}
}
