/*************************************************************************************************************
\file       heightfield.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares HeightField, the float height grid the terrain prepass works on, its smooth and
            linear resamplers and spike clamp, and TerrainMaps, the per-column result handed to the fill.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Voxel
#include "../types/voxel_types.hpp"

// stds
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>


namespace voxel
{
	/**
	 * A row-major grid of heights in metres, the working surface of the
	 * terrain prepass. Row z, column x; cell_ is the spacing in metres.
	 *
	 */
	struct HeightField
	{
		int					width_	{ 0 };
		int					depth_	{ 0 };
		float				cell_	{ 1.f };
		std::vector<float>	h_;

		HeightField() = default;

		/*!
			@brief
				Builds a width x depth grid with every height set to fill.
			@param width
				Columns along x
			@param depth
				Rows along z
			@param cell
				Spacing between samples in metres
			@param fill
				Initial height of every sample
			*/
		HeightField(int width, int depth, float cell, float fill = 0.f)
			: width_(width), depth_(depth), cell_(cell)
			, h_(static_cast<std::size_t>(width) * static_cast<std::size_t>(depth), fill)
		{
		}

		/*!
			@brief
				Row-major index of a sample.
			@param x
				Column
			@param z
				Row
			@return
				z * width_ + x
			*/
		std::size_t index(int x, int z) const
		{
			return static_cast<std::size_t>(z) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x);
		}

		/*!
			@brief
				Mutable access to a sample.
			@param x
				Column
			@param z
				Row
			@return
				Reference to the height at (x, z)
			*/
		float& at(int x, int z) { return h_[index(x, z)]; }
		/*!
			@brief
				Reads a sample.
			@param x
				Column
			@param z
				Row
			@return
				The height at (x, z)
			*/
		float at(int x, int z) const { return h_[index(x, z)]; }

		/*!
			@brief
				at() with the coordinates clamped onto the grid.
			@param x
				Column, clamped to [0, width_ - 1]
			@param z
				Row, clamped to [0, depth_ - 1]
			@return
				The height at the clamped sample
			*/
		float clamped(int x, int z) const
		{
			return h_[index(std::clamp(x, 0, width_ - 1), std::clamp(z, 0, depth_ - 1))];
		}

		/*!
			@brief
				Number of samples.
			@return
				width_ * depth_
			*/
		std::size_t size() const { return h_.size(); }
	};

	/*!
		@brief
			Resamples src onto a width x depth grid with Catmull-Rom interpolation.

			Destination sample x reads source coordinate x * scale + offset.
			Catmull-Rom, because bilinear leaves a crease along every source cell
			edge and a crease quantises into a visible line of voxel steps.
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
	HeightField resampleSmooth(const HeightField& src, int width, int depth, float cell, float scale, float offset);

	/*!
		@brief
			Same mapping as resampleSmooth but bilinear: never overshoots, for masks.
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
	HeightField resampleLinear(const HeightField& src, int width, int depth, float cell, float scale, float offset);

	/*!
		@brief
			Lowers any sample standing more than maxStep above all four of its neighbours.

			After erosion this should change nothing; it is the guarantee that
			no column ends up as a lone spike.
		@param field
			Clamped in place
		@param maxStep
			Largest rise allowed above the highest neighbour, in metres
		@return
			Samples lowered
		*/
	std::size_t clampSpikes(HeightField& field, float maxStep);

	/**
	 * What the prepass hands to the column fill, one entry per voxel column
	 * (row-major, z * width_ + x). Depths are in voxels, measured down from
	 * the surface voxel: topDepth_ of topMaterial_, then soilDepth_ of the
	 * recipe's under material, then base.
	 *
	 */
	struct TerrainMaps
	{
		int							width_	{ 0 };
		int							depth_	{ 0 };

		std::vector<std::int16_t>	surface_;		// highest solid terrain voxel
		std::vector<std::int16_t>	waterTop_;		// highest water voxel, -1 where dry
		std::vector<MaterialId>		topMaterial_;
		std::vector<std::uint8_t>	topDepth_;
		std::vector<std::uint8_t>	soilDepth_;

		// Biome terrain only, empty otherwise: which of the recipe's biomes
		// each column takes its materials from, and how many voxels that
		// biome's rock bands are shifted up at this column.
		std::vector<std::uint8_t>	biome_;
		std::vector<std::int8_t>	strataShift_;

		HeightField					height_;		// final float surface, for previews and checks

		/*!
			@brief
				Row-major index of a voxel column.
			@param x
				Column
			@param z
				Row
			@return
				z * width_ + x
			*/
		std::size_t index(int x, int z) const
		{
			return static_cast<std::size_t>(z) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x);
		}
	};
}
