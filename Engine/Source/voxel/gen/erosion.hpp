/*************************************************************************************************************
\file       erosion.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares the terrain erosion passes: droplet hydraulic erosion and talus-angle thermal
            erosion over a HeightField, both deterministic for a given input and seed.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Voxel
#include "heightfield.hpp"
#include "recipe.hpp"

// stds
#include <atomic>
#include <cstdint>


namespace voxel
{
	/** Droplets simulated per progress tick and cancel poll. */
	inline constexpr int kDropletBatch = 2000;

	/*!
		@brief
			Droplet hydraulic erosion (Beyer 2015, as popularised by Lague).

			Each droplet runs downhill carrying sediment: it digs where it is
			fast and under capacity, and drops its load where it slows. That is
			what cuts gullies into slopes and fills valley floors flat.

			Droplets see what earlier droplets did, so this is sequential by
			nature. It runs on one thread, droplet i starting where
			Pcg32(seed, i) puts it, which makes the result a function of the
			inputs alone whatever the machine's core count.
		@param field
			Eroded in place
		@param sediment
			Same grid; receives the net material gained (+) or lost (-) per
			cell in metres, added to what is already there
		@param params
			Erosion parameters from the recipe
		@param seed
			Seed for the droplet start positions
		@param progress
			Incremented once per kDropletBatch droplets, if given
		@param cancel
			Polled every kDropletBatch droplets; stops the pass when set
		@param erodibility
			Same grid, 0..1: the share of its digging a droplet keeps in each
			cell. Null means 1 everywhere
		@return
			false if cancelled part-way
		*/
	bool erodeHydraulic(HeightField& field, HeightField& sediment, const Recipe::Erosion& params, std::uint64_t seed,
						std::atomic<int>* progress = nullptr, const std::atomic<bool>* cancel = nullptr,
						const HeightField* erodibility = nullptr);

	/*!
		@brief
			Progress ticks erodeHydraulic will report for these parameters.
		@param params
			Erosion parameters from the recipe
		@return
			The number of ticks
		*/
	int hydraulicTicks(const Recipe::Erosion& params);

	/*!
		@brief
			Thermal erosion (Musgrave 1989): excess slope above the talus angle slides down.

			Wherever a cell stands above a neighbour by more than the talus
			angle allows, the excess slides down. Loose sediment rests at a
			gentler angle than bedrock, so cliffs survive while deposits slump
			into scree.

			A fixed number of whole-grid passes, each computed from the previous
			pass alone, so the result does not depend on scan order.
		@param field
			Eroded in place
		@param sediment
			Picks the talus angle per cell and follows the material that moves
		@param params
			Erosion parameters from the recipe
		@param cancel
			Polled once per pass; stops the pass when set
		@param rockTalus
			Same grid: the tangent of the slope bedrock holds in each cell. Null
			means params.rockTalusDeg_ everywhere
		@return
			false if cancelled part-way
		*/
	bool erodeThermal(HeightField& field, HeightField& sediment, const Recipe::Erosion& params,
					  const std::atomic<bool>* cancel = nullptr, const HeightField* rockTalus = nullptr);
}
