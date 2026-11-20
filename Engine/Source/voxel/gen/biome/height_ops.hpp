/*************************************************************************************************************
\file       height_ops.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      The height op registry (the landform shapes a biome layer can use) and CompiledLayer, a
            HeightLayer resolved against its op and ready to sample.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Voxel
#include "biome_definition.hpp"

// Noise
#include "../../../util/noise/fbm.hpp"

// stds
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>


namespace voxel
{
	class CompiledLayer;

	/** A parameter only one op understands, as HeightLayer::params_ names it. */
	struct HeightOpParam
	{
		const char*	name_;
		double		fallback_;
		double		min_, max_;
	};

	/**
	 * A height op: a pure function of a point in metres, returning a shape
	 * in a fixed range that the layer then scales to its amplitude.
	 *
	 * This table is where a new kind of landform is added: write the eval
	 * function, list its parameters, add one entry in height_ops.cpp. The
	 * biome parser validates against the table and the recipe format carries
	 * the op by name, so nothing else changes.
	 *
	 */
	struct HeightOp
	{
		static constexpr std::size_t kMaxParams = 4;

		const char*		name_;
		const char*		summary_;
		HeightOpParam	params_[kMaxParams];
		std::size_t		paramCount_;
		double			min_, max_;		// range of eval_
		double			(*eval_)(const CompiledLayer& layer, double x, double z);
	};

	/*!
		@brief
			Gets every registered op, in a fixed order.
		@return
			The op table
		*/
	std::span<const HeightOp> heightOps();

	/*!
		@brief
			Looks a height op up by name.
		@param name
			The op's name
		@return
			The op, or nullptr when no op has that name
		*/
	const HeightOp* findHeightOp(std::string_view name);

	/**
	 * A HeightLayer made ready to sample: op looked up, parameters resolved
	 * against the op's defaults and ranges, octaves that the grid cannot
	 * resolve dropped, one seed derived per noise field.
	 *
	 */
	class CompiledLayer
	{
	public:

		/*!
			@brief
				Compiles a height layer: looks up its op, resolves parameters and drops unresolvable octaves.

				Throws std::runtime_error for an unknown op, an unknown parameter or
				a parameter out of range.
			@param layer
				The layer definition to compile
			@param seed
				Seed of this layer; its warp and mask derive their own from it
			@param minWavelength
				Octaves finer than this (metres) are dropped
			*/
		CompiledLayer(const HeightLayer& layer, std::uint64_t seed, double minWavelength);

		/*!
			@brief
				Samples the layer's height contribution at a point.
			@param x
				X position in metres
			@param z
				Z position in metres
			@return
				Height contribution in metres
			*/
		double sample(double x, double z) const;

		/*!
			@brief
				Gets the lower bound of sample(), from the op's range rather than by sampling.
			@return
				The lower bound in metres
			*/
		double low() const { return low_; }

		/*!
			@brief
				Gets the upper bound of sample(), from the op's range rather than by sampling.
			@return
				The upper bound in metres
			*/
		double high() const { return high_; }

		/*!
			@brief
				Gets the layer's noise seed; read by an op's eval function.
			@return
				The seed
			*/
		std::uint64_t seed() const { return seed_; }

		/*!
			@brief
				Gets the layer's fbm settings; read by an op's eval function.
			@return
				The fbm parameters
			*/
		const util::noise::FbmParams& fbm() const { return fbm_; }

		/*!
			@brief
				Gets the layer's wavelength; read by an op's eval function.
			@return
				The wavelength in metres
			*/
		double wavelength() const { return wavelength_; }

		/*!
			@brief
				Gets a parameter by its position in the op's params_ list.
			@param index
				Position in the op's params_ list
			@return
				The resolved parameter value
			*/
		double param(std::size_t index) const { return params_[index]; }

	private:

		const HeightOp*							op_			{ nullptr };
		std::uint64_t							seed_		{ 0 };
		std::uint64_t							seedWarp_	{ 0 };
		std::uint64_t							seedMask_	{ 0 };

		util::noise::FbmParams					fbm_;
		util::noise::FbmParams					warpFbm_;
		util::noise::FbmParams					maskFbm_;

		double									wavelength_		{ 1.0 };
		double									amplitude_		{ 0.0 };
		double									warpAmplitude_	{ 0.0 };
		double									maskFloor_		{ 1.0 };
		bool									masked_			{ false };

		std::array<double, HeightOp::kMaxParams>	params_		{};
		double									low_	{ 0.0 }, high_	{ 0.0 };
	};
}
