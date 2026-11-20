/*************************************************************************************************************
\file       hydrology.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Implements priority-flood depression filling and lake finding for the terrain prepass.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "hydrology.hpp"

// Profiling
#include "../../profile/profiler.hpp"

// stds
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <queue>
#include <utility>
#include <vector>


namespace voxel
{
	namespace
	{
		// Depth below which a cell does not count as part of a depression.
		constexpr float kWetEps = 1.0e-4f;

		// Lakes drain the ground above them, which can expose depressions
		// that the first flood saw only as part of a larger one.
		constexpr int kLakeRounds = 3;

		constexpr int kDx[4] = { -1, 1, 0, 0 };
		constexpr int kDz[4] = { 0, 0, -1, 1 };

		using Entry = std::pair<float, std::uint32_t>;	// (height, cell index): a total order

		/*!
			@brief
				Priority-flood from the map edge and from every cell that already
				holds water, which drains at its water level.
			@param field
				Ground heights
			@param water
				Current water levels, or null for none
			@return
				The height each cell must reach to drain
			*/
		HeightField flood(const HeightField& field, const HeightField* water)
		{
			const int width = field.width_, depth = field.depth_;
			HeightField filled = field;
			std::vector<std::uint8_t> closed(field.size(), 0);
			std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;

			for (int z = 0; z < depth; ++z)
			for (int x = 0; x < width; ++x)
			{
				const std::size_t i = field.index(x, z);
				const bool edge = x == 0 || z == 0 || x == width - 1 || z == depth - 1;
				const bool wet = water && water->h_[i] > kNoWater * 0.5f;
				if (!edge && !wet)
					continue;

				if (wet)
					filled.h_[i] = std::max(filled.h_[i], water->h_[i]);
				closed[i] = 1;
				open.emplace(filled.h_[i], static_cast<std::uint32_t>(i));
			}

			while (!open.empty())
			{
				const Entry top = open.top();
				open.pop();

				const int x = static_cast<int>(top.second % static_cast<std::uint32_t>(width));
				const int z = static_cast<int>(top.second / static_cast<std::uint32_t>(width));

				for (int n = 0; n < 4; ++n)
				{
					const int nx = x + kDx[n], nz = z + kDz[n];
					if (nx < 0 || nz < 0 || nx >= width || nz >= depth)
						continue;

					const std::size_t j = field.index(nx, nz);
					if (closed[j])
						continue;

					closed[j] = 1;
					filled.h_[j] = std::max(field.h_[j], top.first);
					open.emplace(filled.h_[j], static_cast<std::uint32_t>(j));
				}
			}

			return filled;
		}
	}

	/*!
		@brief
			Priority-flood depression filling from the map edge.
		@param field
			Ground heights
		@return
			The filled surface, never below field
		*/
	HeightField fillDepressions(const HeightField& field)
	{
		return flood(field, nullptr);
	}

	/*!
		@brief
			Levels small depressions and fills the rest with water, over up to kLakeRounds floods.
		@param field
			Pits are filled in place
		@param params
			Water settings from the recipe
		@return
			Water surface height per cell in metres, kNoWater where dry
		*/
	HeightField findLakes(HeightField& field, const Recipe::Water& params)
	{
		VOX_PROFILE_FUNCTION_C(profile::colour::kVoxel);

		const int width = field.width_, depth = field.depth_;
		HeightField water(width, depth, field.cell_, kNoWater);
		if (!params.enabled_)
			return water;

		const float cellArea = field.cell_ * field.cell_;
		std::vector<std::uint8_t> seen(field.size());
		std::vector<std::uint32_t> members, stack;
		std::vector<float> heights;

		for (int round = 0; round < kLakeRounds; ++round)
		{
			const HeightField filled = flood(field, round == 0 ? nullptr : &water);
			std::fill(seen.begin(), seen.end(), std::uint8_t{ 0 });
			bool changed = false;

			const auto wet = [&](std::size_t i)
			{
				return water.h_[i] <= kNoWater * 0.5f && filled.h_[i] - field.h_[i] > kWetEps;
			};

			for (std::size_t start = 0; start < field.size(); ++start)
			{
				if (seen[start] || !wet(start))
					continue;

				// One depression: connected wet cells sharing a spill height.
				const float spill = filled.h_[start];
				float floor = field.h_[start];
				members.clear();
				stack.assign(1, static_cast<std::uint32_t>(start));
				seen[start] = 1;

				while (!stack.empty())
				{
					const std::uint32_t c = stack.back();
					stack.pop_back();
					members.push_back(c);
					floor = std::min(floor, field.h_[c]);

					const int x = static_cast<int>(c % static_cast<std::uint32_t>(width));
					const int z = static_cast<int>(c / static_cast<std::uint32_t>(width));
					for (int n = 0; n < 4; ++n)
					{
						const int nx = x + kDx[n], nz = z + kDz[n];
						if (nx < 0 || nz < 0 || nx >= width || nz >= depth)
							continue;

						const std::size_t j = field.index(nx, nz);
						if (seen[j] || !wet(j) || std::fabs(filled.h_[j] - spill) > kWetEps)
							continue;
						seen[j] = 1;
						stack.push_back(static_cast<std::uint32_t>(j));
					}
				}

				const float fullDepth = spill - floor;
				const float area = static_cast<float>(members.size()) * cellArea;

				if (fullDepth < params.minDepth_ || area < params.minArea_)
				{
					// An erosion pit, not a lake: level it off.
					for (const std::uint32_t c : members)
						field.h_[c] = spill;
					changed = true;
					continue;
				}

				float level = floor + std::min(fullDepth * params.fillFraction_, params.maxDepth_);

				// Too wide a lake keeps to the deepest part of its basin: the level
				// drops to the height of the cell that would be one too many.
				const std::size_t most = params.maxArea_ > 0.f ? std::max<std::size_t>(1, static_cast<std::size_t>(params.maxArea_ / cellArea)) : members.size();
				if (members.size() > most)
				{
					heights.clear();
					for (const std::uint32_t c : members)
						heights.push_back(field.h_[c]);
					std::nth_element(heights.begin(), heights.begin() + static_cast<std::ptrdiff_t>(most), heights.end());
					level = std::min(level, heights[most]);
				}

				for (const std::uint32_t c : members)
				{
					if (field.h_[c] < level)
					{
						water.h_[c] = level;
						changed = true;
					}
				}
			}

			if (!changed)
				break;
		}

		return water;
	}
}
