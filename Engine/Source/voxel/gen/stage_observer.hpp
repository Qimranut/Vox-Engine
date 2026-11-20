/*************************************************************************************************************
\file       stage_observer.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares the terrain prepass stages (GenStage, kGenStages), the per-stage view of the
            generator's working buffers, and GenStageObserver, which watches buildMaps pass by pass.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Voxel
#include "heightfield.hpp"

// stds
#include <array>
#include <cstdint>
#include <string_view>


namespace voxel
{
	/**
	 * The passes of the terrain prepass, in the order they run. What a
	 * pipeline view shows one card for.
	 *
	 */
	enum class GenStage : std::uint8_t
	{
		Base,			// the landform, before anything has worn it
		Hydraulic,		// droplets: gullies cut, valley floors filled
		Thermal,		// slopes relaxed to their talus angle
		Lakes,			// depressions flooded
		Detail,			// upsampled to one sample per voxel column, roughened where steep
		Materials,		// the surface rules: what each column is made of
		Count
	};

	struct GenStageInfo
	{
		GenStage			id_;
		std::string_view	name_;
		std::string_view	does_;		// one line, for a tooltip
	};

	/** Every stage, in enum order. */
	inline constexpr std::array<GenStageInfo, static_cast<std::size_t>(GenStage::Count)> kGenStages{ {
		{ GenStage::Base,		"Base",			"The landform as its noise and biomes shape it, before any erosion." },
		{ GenStage::Hydraulic,	"Hydraulic",	"Droplets run downhill: gullies are cut into slopes and valley floors fill with what they carry." },
		{ GenStage::Thermal,	"Thermal",		"Slopes steeper than the material can hold slide down: cliffs stay, deposits slump into scree." },
		{ GenStage::Lakes,		"Lakes",		"Depressions are flooded, up to the depth and area the recipe allows." },
		{ GenStage::Detail,		"Detail",		"Upsampled to one sample per voxel column, with roughness added where the ground is steep." },
		{ GenStage::Materials,	"Materials",	"The surface rules: sand, snow, rock, soil or grass per column, by slope, height and sediment." },
	} };

	/**
	 * What the prepass looks like after one stage. Every pointer is into
	 * the generator's own working buffers, which the next stage overwrites:
	 * an observer copies what it wants to keep before it returns.
	 *
	 */
	struct GenStageView
	{
		GenStage			stage_			{ GenStage::Base };
		const HeightField*	height_			{ nullptr };	// the ground surface; coarse grid until Detail, then one sample per column
		const HeightField*	sediment_		{ nullptr };	// net material gained (+) or lost (-) so far, same grid; null before erosion
		const HeightField*	water_			{ nullptr };	// water level per cell, kNoWater where dry; null before Lakes
		const TerrainMaps*	maps_			{ nullptr };	// Materials only
		double				milliseconds_	{ 0. };			// what the stage itself took
	};

	/**
	 * Watches TerrainGenerator::buildMaps go by. Called on whichever thread
	 * runs the prepass, between stages, never concurrently. Passing none
	 * costs nothing and changes nothing: the terrain is the same either way.
	 *
	 */
	class GenStageObserver
	{
	public:
		virtual ~GenStageObserver() = default;

		/*!
			@brief
				Called after each stage with a view of the terrain as that stage left it.
			@param view
				The stage and pointers into the generator's working buffers; copy what is kept
			*/
		virtual void onStage(const GenStageView& view) = 0;
	};
}
