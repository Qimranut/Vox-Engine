/*************************************************************************************************************
\file       stage_preview.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares GL-free previews of the terrain prepass: StageCapture copies of each stage, the
            CapturingObserver that keeps them, and render(), which draws one as a top-down RGBA image.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Voxel
#include "../heightfield.hpp"
#include "../recipe.hpp"
#include "../stage_observer.hpp"
#include "../../types/material_table.hpp"

// GLM
#include <glm/glm.hpp>

// stds
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>


namespace voxel::preview
{
	/**
	 * What one stage of the prepass left behind, copied out of the
	 * generator's working buffers and thinned to at most a few hundred
	 * samples a side: enough for a picture, small enough to keep all six.
	 * Plain data, so a worker thread can build it and hand it to whoever draws.
	 *
	 */
	struct StageCapture
	{
		GenStage					stage_			{ GenStage::Base };
		double						milliseconds_	{ 0. };

		HeightField					height_;		// cell_ is the spacing of the kept samples, in metres
		HeightField					sediment_;		// empty when the stage has none
		HeightField					water_;			// empty when the stage has none; kNoWater where dry
		std::vector<MaterialId>		material_;		// what a column shows from above; empty before Materials
		std::vector<std::uint8_t>	biome_;			// index into the recipe's biomes; empty unless biome terrain

		// A few numbers worth showing beside the picture.
		float						lowest_			{ 0.f };	// metres
		float						highest_		{ 0.f };
		float						wetShare_		{ 0.f };	// 0..1 of the map under water
		double						movedVolume_	{ 0. };		// m^3 of ground this stage raised or lowered; 0 for the first
	};

	/*!
		@brief
			Copies a stage as the generator shows it.
		@param view
			The stage and its working buffers
		@param previous
			The stage before it, to measure what this one moved; null for the first
		@param water
			Material shown for flooded columns
		@param maxSide
			Samples kept along the longer side
		@return
			The thinned copy with its summary numbers
		*/
	StageCapture capture(const GenStageView& view, const StageCapture* previous, MaterialId water, int maxSide = 384);

	/** An observer that keeps every stage it is shown. */
	class CapturingObserver final : public GenStageObserver
	{
	public:
		/*!
			@brief
				An observer that captures every stage.
			@param water
				Material shown for flooded columns
			@param maxSide
				Samples kept along the longer side
			*/
		explicit CapturingObserver(MaterialId water, int maxSide = 384) : water_(water), maxSide_(maxSide) {}
		/*!
			@brief
				Captures the stage, measured against the one before it.
			@param view
				The stage and its working buffers
			*/
		void onStage(const GenStageView& view) override;
		/*!
			@brief
				The stages captured so far, in order.
			@return
				Reference to the captured stages
			*/
		std::vector<StageCapture>& stages() { return stages_; }
	private:
		MaterialId					water_;
		int							maxSide_;
		std::vector<StageCapture>	stages_;
	};

	enum class Mode : std::uint8_t
	{
		Shaded,		// the terrain lit from the north-west; in its materials once it has them
		Height,		// height as a colour ramp, on one scale for every stage
		Change,		// what the stage moved: lowered ground red, raised ground blue
		Water,		// depth of water over the shaded ground
		Biomes,		// which biome each column belongs to
		Count
	};

	/*!
		@brief
			Display name of a mode.
		@param mode
			The mode
		@return
			Its name, or empty for Count
		*/
	std::string_view modeName(Mode mode);

	/*!
		@brief
			Whether a stage has what a mode draws.
		@param stage
			The captured stage
		@param previous
			The stage before it, or null
		@param mode
			The mode to test
		@return
			true when the mode can be drawn
		*/
	bool supports(const StageCapture& stage, const StageCapture* previous, Mode mode);

	/** Top-down picture, row 0 at the north (low z) edge. */
	struct Image
	{
		int							width_	{ 0 };
		int							height_	{ 0 };
		std::vector<unsigned char>	rgba_;		// 8-bit sRGB, opaque
	};

	struct RenderOptions
	{
		Mode					mode_		{ Mode::Shaded };
		float					lowest_		{ 0.f };	// Height: the ramp's range, shared by all stages so they compare
		float					highest_	{ 1.f };
		std::span<const glm::vec3> biomeColours_;		// Biomes: sRGB, indexed like the recipe's biomes
	};

	/*!
		@brief
			Draws a captured stage as a top-down picture; falls back to Shaded when the mode is unsupported.
		@param stage
			The captured stage
		@param previous
			For Mode::Change; may be null otherwise
		@param materials
			Colours for the Shaded mode once the stage has materials
		@param options
			Mode, height range and biome colours
		@return
			The RGBA image, empty when the stage has no heights
		*/
	Image render(const StageCapture& stage, const StageCapture* previous, const MaterialTable& materials, const RenderOptions& options);

	/*!
		@brief
			The colours of a recipe's biomes, for RenderOptions; empty unless it is biome terrain.
		@param recipe
			The recipe
		@return
			One sRGB colour per recipe biome
		*/
	std::vector<glm::vec3> biomeColours(const Recipe& recipe);
}
