/*************************************************************************************************************
\file       heightfield.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Implements the HeightField resamplers (Catmull-Rom and bilinear) and the spike clamp.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "heightfield.hpp"

// Profiling
#include "../../profile/profiler.hpp"

// stds
#include <cmath>


namespace voxel
{
	namespace
	{
		/*!
			@brief
				Catmull-Rom through p1..p2 with neighbours p0 and p3.
			@param p0
				Point before p1
			@param p1
				Start of the interpolated span
			@param p2
				End of the interpolated span
			@param p3
				Point after p2
			@param t
				Position along p1..p2, in [0, 1]
			@return
				The interpolated value
			*/
		inline float catmullRom(float p0, float p1, float p2, float p3, float t)
		{
			const float a = -0.5f * p0 + 1.5f * p1 - 1.5f * p2 + 0.5f * p3;
			const float b = p0 - 2.5f * p1 + 2.f * p2 - 0.5f * p3;
			const float c = -0.5f * p0 + 0.5f * p2;
			return ((a * t + b) * t + c) * t + p1;
		}
	}

	/*!
		@brief
			Resamples src onto a width x depth grid with Catmull-Rom interpolation.
		@param src
			Grid to read
		@param width
			Destination columns
		@param depth
			Destination rows
		@param cell
			Destination spacing in metres
		@param scale
			Source cells per destination sample
		@param offset
			Source coordinate of destination sample 0
		@return
			The resampled grid
		*/
	HeightField resampleSmooth(const HeightField& src, int width, int depth, float cell, float scale, float offset)
	{
		VOX_PROFILE_FUNCTION_C(profile::colour::kVoxel);

		HeightField out(width, depth, cell);

		for (int z = 0; z < depth; ++z)
		{
			const float v = static_cast<float>(z) * scale + offset;
			const int z1 = static_cast<int>(std::floor(v));
			const float tz = v - static_cast<float>(z1);

			for (int x = 0; x < width; ++x)
			{
				const float u = static_cast<float>(x) * scale + offset;
				const int x1 = static_cast<int>(std::floor(u));
				const float tx = u - static_cast<float>(x1);

				float rows[4];
				for (int r = 0; r < 4; ++r)
				{
					const int zz = z1 - 1 + r;
					rows[r] = catmullRom(src.clamped(x1 - 1, zz), src.clamped(x1, zz),
										 src.clamped(x1 + 1, zz), src.clamped(x1 + 2, zz), tx);
				}

				out.at(x, z) = catmullRom(rows[0], rows[1], rows[2], rows[3], tz);
			}
		}

		return out;
	}

	/*!
		@brief
			Resamples src onto a width x depth grid with bilinear interpolation.
		@param src
			Grid to read
		@param width
			Destination columns
		@param depth
			Destination rows
		@param cell
			Destination spacing in metres
		@param scale
			Source cells per destination sample
		@param offset
			Source coordinate of destination sample 0
		@return
			The resampled grid
		*/
	HeightField resampleLinear(const HeightField& src, int width, int depth, float cell, float scale, float offset)
	{
		VOX_PROFILE_FUNCTION_C(profile::colour::kVoxel);

		HeightField out(width, depth, cell);

		for (int z = 0; z < depth; ++z)
		{
			const float v = static_cast<float>(z) * scale + offset;
			const int z0 = static_cast<int>(std::floor(v));
			const float tz = v - static_cast<float>(z0);

			for (int x = 0; x < width; ++x)
			{
				const float u = static_cast<float>(x) * scale + offset;
				const int x0 = static_cast<int>(std::floor(u));
				const float tx = u - static_cast<float>(x0);

				const float a = src.clamped(x0, z0) + (src.clamped(x0 + 1, z0) - src.clamped(x0, z0)) * tx;
				const float b = src.clamped(x0, z0 + 1) + (src.clamped(x0 + 1, z0 + 1) - src.clamped(x0, z0 + 1)) * tx;
				out.at(x, z) = a + (b - a) * tz;
			}
		}

		return out;
	}

	/*!
		@brief
			Lowers any sample standing more than maxStep above all four of its neighbours.

			Reads an untouched copy so the result does not depend on scan order.
		@param field
			Clamped in place
		@param maxStep
			Largest rise allowed above the highest neighbour, in metres
		@return
			Samples lowered
		*/
	std::size_t clampSpikes(HeightField& field, float maxStep)
	{
		// Reads the untouched copy so the result does not depend on scan order.
		const HeightField before = field;
		std::size_t lowered = 0;

		for (int z = 0; z < field.depth_; ++z)
		for (int x = 0; x < field.width_; ++x)
		{
			const float highest = std::max({ before.clamped(x - 1, z), before.clamped(x + 1, z),
											 before.clamped(x, z - 1), before.clamped(x, z + 1) });
			if (before.at(x, z) > highest + maxStep)
			{
				field.at(x, z) = highest + maxStep;
				++lowered;
			}
		}

		return lowered;
	}
}
