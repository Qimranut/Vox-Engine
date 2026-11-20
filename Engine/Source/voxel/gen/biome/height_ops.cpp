/*************************************************************************************************************
\file       height_ops.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      The height op table (fbm, damped, ridged, billow, dunes, plateau) with their eval functions,
            and CompiledLayer: parameter resolution, octave culling, bounds, masking and warped sampling.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "height_ops.hpp"

// Noise
#include "../../../util/noise/hash.hpp"

// stds
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>


namespace voxel
{
	namespace
	{
		using namespace util::noise;

		constexpr double kPi = 3.14159265358979323846;

		// ------------------------------ Ops ------------------------------
		//
		// x and z arrive in metres, already warped. Each op returns a shape
		// in the range its table entry declares.

		/*!
			@brief
				The "fbm" op: plain fractal noise.
			@param l
				The compiled layer being sampled
			@param x
				Warped X position in metres
			@param z
				Warped Z position in metres
			@return
				The shape, -1..1
			*/
		double evalFbm(const CompiledLayer& l, double x, double z)
		{
			return fbm2(l.seed(), x, z, l.fbm());
		}

		/*!
			@brief
				The "damped" op: fbm that loses its detail on steep ground.

				params: damping
			@param l
				The compiled layer being sampled
			@param x
				Warped X position in metres
			@param z
				Warped Z position in metres
			@return
				The shape, -1..1
			*/
		double evalDamped(const CompiledLayer& l, double x, double z)
		{
			return dampedFbm2(l.seed(), x, z, l.fbm(), l.param(0));
		}

		/*!
			@brief
				The "ridged" op: smooth ridged noise raised onto a floor.

				params: floor - the share every point gets, ridge or not
			@param l
				The compiled layer being sampled
			@param x
				Warped X position in metres
			@param z
				Warped Z position in metres
			@return
				The shape, 0..1
			*/
		double evalRidged(const CompiledLayer& l, double x, double z)
		{
			const double floor = l.param(0);
			return floor + (1.0 - floor) * ridgedSmooth2(l.seed(), x, z, l.fbm());
		}

		/*!
			@brief
				The "billow" op: rounded mounds with soft creases between them.
			@param l
				The compiled layer being sampled
			@param x
				Warped X position in metres
			@param z
				Warped Z position in metres
			@return
				The shape, -1..1
			*/
		double evalBillow(const CompiledLayer& l, double x, double z)
		{
			return billow2(l.seed(), x, z, l.fbm());
		}

		/*!
			@brief
				The "dunes" op: parallel asymmetric crests one wavelength apart, square to the wind.

				params: windDeg, asymmetry. The layer's warp is what makes the
				crests wander.
			@param l
				The compiled layer being sampled
			@param x
				Warped X position in metres
			@param z
				Warped Z position in metres
			@return
				The shape, 0..1
			*/
		double evalDunes(const CompiledLayer& l, double x, double z)
		{
			const double wind = l.param(0) * kPi / 180.0;
			const double along = x * std::cos(wind) + z * std::sin(wind);
			return duneProfile(along / l.wavelength(), l.param(1));
		}

		/*!
			@brief
				The "plateau" op: a tableland cut by channels.

				params: low, high. 0 along the noise's zero crossings, which form
				long connected lines, rising to 1 between them. low and high set
				how wide the channel floor and its walls are.
			@param l
				The compiled layer being sampled
			@param x
				Warped X position in metres
			@param z
				Warped Z position in metres
			@return
				The shape, 0..1
			*/
		double evalPlateau(const CompiledLayer& l, double x, double z)
		{
			const double n = smoothAbs(fbm2(l.seed(), x, z, l.fbm()));
			return smoothstep(l.param(0), l.param(1), n);
		}

		constexpr HeightOp kOps[] =
		{
			{ "fbm", "rolling noise", {}, 0, -1.0, 1.0, &evalFbm },
			{ "damped", "noise that loses its detail on steep ground",
				{ { "damping", 0.35, 0.0, 8.0 } }, 1, -1.0, 1.0, &evalDamped },
			{ "ridged", "mountain ridges with rounded crests",
				{ { "floor", 0.3, 0.0, 1.0 } }, 1, 0.0, 1.0, &evalRidged },
			{ "billow", "rounded mounds with soft creases between them", {}, 0, -1.0, 1.0, &evalBillow },
			{ "dunes", "parallel asymmetric ridges square to a wind direction",
				{ { "windDeg", 25.0, -360.0, 360.0 }, { "asymmetry", 0.72, 0.05, 0.95 } }, 2, 0.0, 1.0, &evalDunes },
			{ "plateau", "tableland cut by winding channels",
				{ { "low", 0.10, 0.0, 1.0 }, { "high", 0.30, 0.0, 1.0 } }, 2, 0.0, 1.0, &evalPlateau },
		};

		/*!
			@brief
				Builds fbm settings, keeping only the octaves whose wavelength is still resolvable.
			@param wavelength
				Wavelength of the first octave in metres
			@param octaves
				Most octaves wanted
			@param lacunarity
				Frequency multiplier per octave
			@param gain
				Amplitude multiplier per octave
			@param minWavelength
				Octaves finer than this (metres) are dropped
			@return
				The fbm parameters
			*/
		FbmParams fbmFor(double wavelength, int octaves, double lacunarity, double gain, double minWavelength)
		{
			FbmParams p;
			p.frequency_ = 1.0 / wavelength;
			p.lacunarity_ = lacunarity;
			p.gain_ = gain;

			// Keep the octaves whose wavelength is still resolvable.
			p.octaves_ = 1;
			double w = wavelength / lacunarity;
			while (p.octaves_ < octaves && w >= minWavelength)
			{
				++p.octaves_;
				w /= lacunarity;
			}
			return p;
		}
	}

	/*!
		@brief
			Gets every registered op, in a fixed order.
		@return
			The op table
		*/
	std::span<const HeightOp> heightOps()
	{
		return kOps;
	}

	/*!
		@brief
			Looks a height op up by name.
		@param name
			The op's name
		@return
			The op, or nullptr when no op has that name
		*/
	const HeightOp* findHeightOp(std::string_view name)
	{
		for (const HeightOp& op : kOps)
			if (name == op.name_)
				return &op;
		return nullptr;
	}

	/*!
		@brief
			Compiles a height layer: looks up its op, builds its noise settings, resolves parameters and bounds.

			Throws std::runtime_error for an unknown op, an unknown parameter or
			a parameter out of range.
		@param layer
			The layer definition to compile
		@param seed
			Seed of this layer; its warp and mask derive their own from it
		@param minWavelength
			Octaves finer than this (metres) are dropped
		*/
	CompiledLayer::CompiledLayer(const HeightLayer& layer, std::uint64_t seed, double minWavelength)
		: op_(findHeightOp(layer.op_))
		, seed_(seed)
		, seedWarp_(seed ^ hashString("layer.warp"))
		, seedMask_(seed ^ hashString("layer.mask"))
		, wavelength_(layer.wavelength_)
		, amplitude_(layer.amplitude_)
		, warpAmplitude_(layer.warp_.amplitude_)
	{
		if (op_ == nullptr)
			throw std::runtime_error("unknown height op \"" + layer.op_ + "\"");
		if (!(layer.wavelength_ > 0.0))
			throw std::runtime_error("height op \"" + layer.op_ + "\": wavelength must be positive");

		fbm_ = fbmFor(layer.wavelength_, layer.octaves_, layer.lacunarity_, layer.gain_, minWavelength);

		if (warpAmplitude_ != 0.0)
		{
			if (!(layer.warp_.wavelength_ > 0.0))
				throw std::runtime_error("height op \"" + layer.op_ + "\": warp wavelength must be positive");
			warpFbm_ = fbmFor(layer.warp_.wavelength_, 2, 2.0, 0.5, minWavelength);
		}

		if (layer.mask_.wavelength_ > 0.0)
		{
			masked_ = true;
			maskFloor_ = std::clamp(layer.mask_.floor_, 0.0, 1.0);
			maskFbm_ = fbmFor(layer.mask_.wavelength_, 2, 2.0, 0.5, minWavelength);
		}

		for (std::size_t p = 0; p < op_->paramCount_; ++p)
			params_[p] = op_->params_[p].fallback_;

		for (const auto& [name, value] : layer.params_)
		{
			std::size_t p = 0;
			while (p < op_->paramCount_ && name != op_->params_[p].name_)
				++p;
			if (p == op_->paramCount_)
				throw std::runtime_error("height op \"" + layer.op_ + "\" has no parameter \"" + name + "\"");

			const HeightOpParam& spec = op_->params_[p];
			if (!(value >= spec.min_ && value <= spec.max_))
				throw std::runtime_error("height op \"" + layer.op_ + "\": \"" + name + "\" must be "
					+ std::to_string(spec.min_) + ".." + std::to_string(spec.max_));
			params_[p] = value;
		}

		// A mask only ever shrinks the layer, so it matters to the bounds
		// only where the op cannot go negative.
		const double a = std::fabs(amplitude_);
		low_ = a * (op_->min_ < 0.0 ? op_->min_ : op_->min_ * (masked_ ? maskFloor_ : 1.0));
		high_ = a * op_->max_;
		if (amplitude_ < 0.0)
		{
			const double flipped = -low_;
			low_ = -high_;
			high_ = flipped;
		}
	}

	/*!
		@brief
			Samples the layer: applies the mask to the amplitude, warps the point, then evaluates the op.
		@param x
			X position in metres
		@param z
			Z position in metres
		@return
			Height contribution in metres
		*/
	double CompiledLayer::sample(double x, double z) const
	{
		double amplitude = amplitude_;
		if (masked_)
		{
			const double m = std::clamp(0.5 + 0.5 * fbm2(seedMask_, x, z, maskFbm_), 0.0, 1.0);
			amplitude *= maskFloor_ + (1.0 - maskFloor_) * m;
		}

		double wx = x, wz = z;
		if (warpAmplitude_ != 0.0)
			domainWarp2(seedWarp_, x, z, warpAmplitude_, warpFbm_, wx, wz);

		return amplitude * op_->eval_(*this, wx, wz);
	}
}
