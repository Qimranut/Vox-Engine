/*************************************************************************************************************
\file       stage_preview.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Implements stage capture (thinning, summary numbers) and the top-down preview renderer
            (Shaded, Height, Change, Water and Biomes views) shared by VoxelProbe and the Editor.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "stage_preview.hpp"

// Voxel
#include "../hydrology.hpp"

// Profiling
#include "../../../profile/profiler.hpp"

// stds
#include <algorithm>
#include <cmath>
#include <iterator>


namespace voxel::preview
{
	namespace
	{
		/*!
			@brief
				Whether a water-level value holds water.
			@param level
				Water level, kNoWater where dry
			@return
				true when wet
			*/
		bool wet(float level) { return level > kNoWater * 0.5f; }

		/*!
			@brief
				Every stride-th sample of a field.

				Keeps the spacing honest so a slope measured on the copy is the
				slope of the terrain.
			@param src
				Field to thin
			@param stride
				Keep one sample in this many along each axis
			@return
				The thinned field
			*/
		HeightField thinned(const HeightField& src, int stride)
		{
			const int width = (src.width_ + stride - 1) / stride, depth = (src.depth_ + stride - 1) / stride;
			HeightField out(width, depth, src.cell_ * static_cast<float>(stride));
			for (int z = 0; z < depth; ++z)
			for (int x = 0; x < width; ++x)
				out.at(x, z) = src.at(std::min(x * stride, src.width_ - 1), std::min(z * stride, src.depth_ - 1));
			return out;
		}

		/*!
			@brief
				A field read at a position given as a share of the map.

				Lets two stages on different grids be compared sample for sample.
			@param field
				Field to read
			@param u
				Position across x, 0..1
			@param v
				Position across z, 0..1
			@return
				The nearest sample's height
			*/
		float sampleAt(const HeightField& field, float u, float v)
		{
			const int x = std::clamp(static_cast<int>(u * static_cast<float>(field.width_)), 0, field.width_ - 1);
			const int z = std::clamp(static_cast<int>(v * static_cast<float>(field.depth_)), 0, field.depth_ - 1);
			return field.at(x, z);
		}

		/*!
			@brief
				Hillshade with light from the north-west, 45 degrees up.
			@param h
				Heights
			@param x
				Column
			@param z
				Row
			@return
				Brightness in [0.3, 1]
			*/
		float hillshade(const HeightField& h, int x, int z)
		{
			const float inv = 1.f / (2.f * h.cell_);
			const float gx = (h.clamped(x + 1, z) - h.clamped(x - 1, z)) * inv;
			const float gz = (h.clamped(x, z + 1) - h.clamped(x, z - 1)) * inv;
			const float lit = (0.5f * gx + 0.5f * gz + 0.7071f) / std::sqrt(1.f + gx * gx + gz * gz);
			return 0.3f + 0.7f * std::clamp(lit, 0.f, 1.f);
		}

		/*!
			@brief
				Height colour ramp: dark blue through green and yellow to white, low to high.
			@param t
				Position on the ramp, clamped to [0, 1]
			@return
				The sRGB colour
			*/
		glm::vec3 heightRamp(float t)
		{
			constexpr glm::vec3 kStops[] = {
				{ 0.10f, 0.16f, 0.36f }, { 0.13f, 0.44f, 0.46f }, { 0.36f, 0.66f, 0.34f },
				{ 0.86f, 0.80f, 0.42f }, { 0.62f, 0.42f, 0.30f }, { 0.97f, 0.97f, 0.97f } };
			constexpr int kLast = static_cast<int>(std::size(kStops)) - 1;

			const float scaled = std::clamp(t, 0.f, 1.f) * static_cast<float>(kLast);
			const int i = std::min(static_cast<int>(scaled), kLast - 1);
			return glm::mix(kStops[i], kStops[i + 1], scaled - static_cast<float>(i));
		}

		const glm::vec3 kBareGround{ 0.74f, 0.71f, 0.66f };		// before the terrain has materials
		const glm::vec3 kWaterShallow{ 0.36f, 0.66f, 0.92f };
		const glm::vec3 kWaterDeep{ 0.08f, 0.22f, 0.55f };

		/*!
			@brief
				Writes one opaque pixel, clamping the colour to [0, 1].
			@param image
				Target image
			@param x
				Pixel column
			@param z
				Pixel row
			@param colour
				sRGB colour
			*/
		void put(Image& image, int x, int z, const glm::vec3& colour)
		{
			const std::size_t i = (static_cast<std::size_t>(z) * static_cast<std::size_t>(image.width_) + static_cast<std::size_t>(x)) * 4;
			const glm::vec3 c = glm::clamp(colour, 0.f, 1.f) * 255.f;
			image.rgba_[i + 0] = static_cast<unsigned char>(c.r);
			image.rgba_[i + 1] = static_cast<unsigned char>(c.g);
			image.rgba_[i + 2] = static_cast<unsigned char>(c.b);
			image.rgba_[i + 3] = 255;
		}
	}

	/*!
		@brief
			Copies a stage as the generator shows it, thinned to maxSide samples along the longer side.
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
	StageCapture capture(const GenStageView& view, const StageCapture* previous, MaterialId water, int maxSide)
	{
		VOX_PROFILE_FUNCTION_C(profile::colour::kVoxel);

		StageCapture out;
		out.stage_ = view.stage_;
		out.milliseconds_ = view.milliseconds_;
		if (!view.height_ || view.height_->size() == 0)
			return out;

		const HeightField& full = *view.height_;
		const int stride = std::max(1, (std::max(full.width_, full.depth_) + maxSide - 1) / std::max(1, maxSide));

		out.height_ = thinned(full, stride);
		if (view.sediment_ && view.sediment_->size() == full.size())
			out.sediment_ = thinned(*view.sediment_, stride);
		if (view.water_ && view.water_->size() == full.size())
			out.water_ = thinned(*view.water_, stride);

		if (view.maps_ && view.maps_->topMaterial_.size() == full.size())
		{
			const TerrainMaps& maps = *view.maps_;
			const int width = out.height_.width_, depth = out.height_.depth_;
			out.material_.resize(out.height_.size());
			if (maps.biome_.size() == full.size())
				out.biome_.resize(out.height_.size());

			for (int z = 0; z < depth; ++z)
			for (int x = 0; x < width; ++x)
			{
				const std::size_t from = maps.index(std::min(x * stride, maps.width_ - 1), std::min(z * stride, maps.depth_ - 1));
				const std::size_t to = out.height_.index(x, z);
				// From above, a flooded column shows its water.
				out.material_[to] = maps.waterTop_[from] >= 0 ? water : maps.topMaterial_[from];
				if (!out.biome_.empty())
					out.biome_[to] = maps.biome_[from];
			}
		}

		const auto [low, high] = std::minmax_element(out.height_.h_.begin(), out.height_.h_.end());
		out.lowest_ = *low;
		out.highest_ = *high;

		if (out.water_.size() != 0)
		{
			std::size_t flooded = 0;
			for (std::size_t i = 0; i < out.water_.size(); ++i)
				if (wet(out.water_.h_[i]) && out.water_.h_[i] > out.height_.h_[i])
					++flooded;
			out.wetShare_ = static_cast<float>(flooded) / static_cast<float>(out.water_.size());
		}

		if (previous && previous->height_.size() != 0)
		{
			const float area = out.height_.cell_ * out.height_.cell_;
			for (int z = 0; z < out.height_.depth_; ++z)
			for (int x = 0; x < out.height_.width_; ++x)
			{
				const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(out.height_.width_);
				const float v = (static_cast<float>(z) + 0.5f) / static_cast<float>(out.height_.depth_);
				out.movedVolume_ += std::fabs(out.height_.at(x, z) - sampleAt(previous->height_, u, v)) * area;
			}
		}
		return out;
	}

	/*!
		@brief
			Captures the stage, measured against the last one captured.
		@param view
			The stage and its working buffers
		*/
	void CapturingObserver::onStage(const GenStageView& view)
	{
		stages_.push_back(capture(view, stages_.empty() ? nullptr : &stages_.back(), water_, maxSide_));
	}

	/*!
		@brief
			Display name of a mode.
		@param mode
			The mode
		@return
			Its name, or empty for Count
		*/
	std::string_view modeName(Mode mode)
	{
		switch (mode)
		{
		case Mode::Shaded:	return "Shaded";
		case Mode::Height:	return "Height";
		case Mode::Change:	return "Change";
		case Mode::Water:	return "Water";
		case Mode::Biomes:	return "Biomes";
		default:			return "";
		}
	}

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
	bool supports(const StageCapture& stage, const StageCapture* previous, Mode mode)
	{
		if (stage.height_.size() == 0)
			return false;

		switch (mode)
		{
		case Mode::Change:	return previous != nullptr && previous->height_.size() != 0;
		case Mode::Water:	return stage.water_.size() != 0;
		case Mode::Biomes:	return !stage.biome_.empty();
		default:			return true;
		}
	}

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
	Image render(const StageCapture& stage, const StageCapture* previous, const MaterialTable& materials, const RenderOptions& options)
	{
		VOX_PROFILE_FUNCTION_C(profile::colour::kVoxel);

		Image image;
		const HeightField& h = stage.height_;
		if (h.size() == 0)
			return image;

		image.width_ = h.width_;
		image.height_ = h.depth_;
		image.rgba_.resize(h.size() * 4);

		const Mode mode = supports(stage, previous, options.mode_) ? options.mode_ : Mode::Shaded;
		const float range = std::max(options.highest_ - options.lowest_, 1e-3f);

		// Change is drawn against the largest move on the map, but never
		// against less than a voxel or so: a stage that moved nothing must
		// not light up with rounding noise.
		float largestChange = 0.1f;
		if (mode == Mode::Change)
		{
			for (int z = 0; z < h.depth_; ++z)
			for (int x = 0; x < h.width_; ++x)
			{
				const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(h.width_);
				const float v = (static_cast<float>(z) + 0.5f) / static_cast<float>(h.depth_);
				largestChange = std::max(largestChange, std::fabs(h.at(x, z) - sampleAt(previous->height_, u, v)));
			}
		}

		for (int z = 0; z < h.depth_; ++z)
		for (int x = 0; x < h.width_; ++x)
		{
			const std::size_t i = h.index(x, z);
			const float shade = hillshade(h, x, z);
			const bool flooded = stage.water_.size() != 0 && wet(stage.water_.h_[i]) && stage.water_.h_[i] > h.h_[i];
			glm::vec3 colour;

			switch (mode)
			{
			case Mode::Height:
				colour = heightRamp((h.h_[i] - options.lowest_) / range) * (0.75f + 0.25f * shade);
				break;

			case Mode::Change:
			{
				const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(h.width_);
				const float v = (static_cast<float>(z) + 0.5f) / static_cast<float>(h.depth_);
				const float delta = (h.h_[i] - sampleAt(previous->height_, u, v)) / largestChange;
				const float amount = std::sqrt(std::min(1.f, std::fabs(delta)));		// small moves still show
				const glm::vec3 ground = glm::vec3{ 0.30f } * shade;
				colour = glm::mix(ground, delta < 0.f ? glm::vec3{ 0.95f, 0.25f, 0.18f } : glm::vec3{ 0.22f, 0.50f, 1.f }, amount);
				break;
			}

			case Mode::Water:
			{
				colour = glm::vec3{ 0.46f } * shade;
				if (flooded)
					colour = glm::mix(kWaterShallow, kWaterDeep, std::min(1.f, (stage.water_.h_[i] - h.h_[i]) / 3.f));
				break;
			}

			case Mode::Biomes:
			{
				const std::size_t biome = stage.biome_[i];
				const glm::vec3 base = biome < options.biomeColours_.size() ? options.biomeColours_[biome] : glm::vec3{ 0.5f };
				colour = base * shade;
				break;
			}

			default:
				if (!stage.material_.empty() && stage.material_[i] < materials.size())
				{
					// Water is a flat sheet: shading it by the lake bed would show the bed.
					const bool sheet = flooded;
					colour = materials.get(stage.material_[i]).colourSrgb_ * (sheet ? 0.8f : shade);
				}
				else
					colour = flooded ? kWaterShallow * 0.85f : kBareGround * shade;
				break;
			}

			put(image, x, z, colour);
		}
		return image;
	}

	/*!
		@brief
			The colours of a recipe's biomes; empty unless it is biome terrain.
		@param recipe
			The recipe
		@return
			One sRGB colour per recipe biome
		*/
	std::vector<glm::vec3> biomeColours(const Recipe& recipe)
	{
		std::vector<glm::vec3> colours;
		if (!recipe.biomes_.enabled_)
			return colours;
		for (const BiomeDefinition& biome : recipe.biomes_.entries_)
			colours.push_back(biome.colour_);
		return colours;
	}
}
