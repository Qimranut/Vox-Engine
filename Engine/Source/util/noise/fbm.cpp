/*************************************************************************************************************
\file       fbm.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Fractal noise built on simplex noise: fbm, ridged, smooth-ridged, slope-damped and billowed
            octave sums, plus the dune profile, terrace fold and domain warp used by terrain generation.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "fbm.hpp"

// Noise
#include "simplex_noise.hpp"
#include "hash.hpp"

// stds
#include <algorithm>
#include <cmath>


namespace util::noise
{
	/*!
		@brief
			Sums octaves of 2D simplex noise with decaying gain, normalised to roughly [-1, 1].
		@param seed
			Field seed; each octave uses seed + octave index
		@param x
			Sample x in input units
		@param y
			Sample y in input units
		@param p
			Fractal parameters (octaves, frequency, lacunarity, gain)
		@return
			The normalised noise value
		*/
	double fbm2(std::uint64_t seed, double x, double y, const FbmParams& p)
	{
		double sum = 0.0, amplitude = 1.0, norm = 0.0, frequency = p.frequency_;

		for (int o = 0; o < std::max(1, p.octaves_); ++o)
		{
			sum += amplitude * noise2(seed + static_cast<std::uint64_t>(o), x * frequency, y * frequency);
			norm += amplitude;
			amplitude *= p.gain_;
			frequency *= p.lacunarity_;
		}

		return norm > 0.0 ? sum / norm : 0.0;
	}

	/*!
		@brief
			Sums octaves of 3D simplex noise with decaying gain, normalised to roughly [-1, 1].
		@param seed
			Field seed; each octave uses seed + octave index
		@param x
			Sample x in input units
		@param y
			Sample y in input units
		@param z
			Sample z in input units
		@param p
			Fractal parameters (octaves, frequency, lacunarity, gain)
		@return
			The normalised noise value
		*/
	double fbm3(std::uint64_t seed, double x, double y, double z, const FbmParams& p)
	{
		double sum = 0.0, amplitude = 1.0, norm = 0.0, frequency = p.frequency_;

		for (int o = 0; o < std::max(1, p.octaves_); ++o)
		{
			sum += amplitude * noise3(seed + static_cast<std::uint64_t>(o), x * frequency, y * frequency, z * frequency);
			norm += amplitude;
			amplitude *= p.gain_;
			frequency *= p.lacunarity_;
		}

		return norm > 0.0 ? sum / norm : 0.0;
	}

	/*!
		@brief
			Ridged multifractal: squared inverted |noise| per octave, each weighted by the previous one.
		@param seed
			Field seed; each octave uses seed + octave index
		@param x
			Sample x in input units
		@param y
			Sample y in input units
		@param p
			Fractal parameters (octaves, frequency, lacunarity, gain)
		@return
			The ridge value, clamped to [0, 1]
		*/
	double ridged2(std::uint64_t seed, double x, double y, const FbmParams& p)
	{
		double sum = 0.0, amplitude = 1.0, norm = 0.0, frequency = p.frequency_, weight = 1.0;

		for (int o = 0; o < std::max(1, p.octaves_); ++o)
		{
			double n = 1.0 - std::fabs(noise2(seed + static_cast<std::uint64_t>(o), x * frequency, y * frequency));
			n *= n;
			n *= weight;
			weight = std::clamp(n * 2.0, 0.0, 1.0);

			sum += n * amplitude;
			norm += amplitude;
			amplitude *= p.gain_;
			frequency *= p.lacunarity_;
		}

		return norm > 0.0 ? std::clamp(sum / norm, 0.0, 1.0) : 0.0;
	}

	/*!
		@brief
			ridged2 with the crest rounded off by using sqrt(n^2 + eps) in place of |n|.
		@param seed
			Field seed; each octave uses seed + octave index
		@param x
			Sample x in input units
		@param y
			Sample y in input units
		@param p
			Fractal parameters (octaves, frequency, lacunarity, gain)
		@return
			The ridge value, clamped to [0, 1]
		*/
	double ridgedSmooth2(std::uint64_t seed, double x, double y, const FbmParams& p)
	{
		constexpr double kCrestEps = 0.004;

		double sum = 0.0, amplitude = 1.0, norm = 0.0, frequency = p.frequency_, weight = 1.0;

		for (int o = 0; o < std::max(1, p.octaves_); ++o)
		{
			const double raw = noise2(seed + static_cast<std::uint64_t>(o), x * frequency, y * frequency);
			double n = std::max(0.0, 1.0 - std::sqrt(raw * raw + kCrestEps));
			n *= n;
			n *= weight;
			weight = std::clamp(n * 2.0, 0.0, 1.0);

			sum += n * amplitude;
			norm += amplitude;
			amplitude *= p.gain_;
			frequency *= p.lacunarity_;
		}

		return norm > 0.0 ? std::clamp(sum / norm, 0.0, 1.0) : 0.0;
	}

	/*!
		@brief
			fbm whose octaves are divided by 1 + damping * |accumulated gradient|^2.
		@param seed
			Field seed; each octave uses seed + octave index
		@param x
			Sample x in input units
		@param y
			Sample y in input units
		@param p
			Fractal parameters (octaves, frequency, lacunarity, gain)
		@param damping
			0 is plain fbm; larger is smoother on slopes
		@return
			The normalised noise value, roughly [-1, 1]
		*/
	double dampedFbm2(std::uint64_t seed, double x, double y, const FbmParams& p, double damping)
	{
		double sum = 0.0, amplitude = 1.0, norm = 0.0, frequency = p.frequency_;
		double gx = 0.0, gy = 0.0;

		for (int o = 0; o < std::max(1, p.octaves_); ++o)
		{
			double dx, dy;
			const double n = noise2Deriv(seed + static_cast<std::uint64_t>(o), x * frequency, y * frequency, dx, dy);

			// Gradients are summed unscaled, as in the reference: what matters
			// is how many octaves agree the ground is steep here.
			gx += dx;
			gy += dy;

			sum += amplitude * n / (1.0 + damping * (gx * gx + gy * gy));
			norm += amplitude;
			amplitude *= p.gain_;
			frequency *= p.lacunarity_;
		}

		return norm > 0.0 ? sum / norm : 0.0;
	}

	/*!
		@brief
			|n| with the corner at zero rounded off: sqrt(n^2 + eps).
		@param n
			Input value
		@param eps
			Rounding amount at zero
		@return
			The smoothed absolute value
		*/
	double smoothAbs(double n, double eps)
	{
		return std::sqrt(n * n + eps);
	}

	/*!
		@brief
			Billowed fbm: octaves of 2 * smoothAbs(n) - 1, giving rounded pillows with soft creases.
		@param seed
			Field seed; each octave uses seed + octave index
		@param x
			Sample x in input units
		@param y
			Sample y in input units
		@param p
			Fractal parameters (octaves, frequency, lacunarity, gain)
		@return
			The normalised value, clamped to [-1, 1]
		*/
	double billow2(std::uint64_t seed, double x, double y, const FbmParams& p)
	{
		double sum = 0.0, amplitude = 1.0, norm = 0.0, frequency = p.frequency_;

		for (int o = 0; o < std::max(1, p.octaves_); ++o)
		{
			const double n = noise2(seed + static_cast<std::uint64_t>(o), x * frequency, y * frequency);
			sum += amplitude * (2.0 * smoothAbs(n) - 1.0);
			norm += amplitude;
			amplitude *= p.gain_;
			frequency *= p.lacunarity_;
		}

		return norm > 0.0 ? std::clamp(sum / norm, -1.0, 1.0) : 0.0;
	}

	/*!
		@brief
			One period of an asymmetric cosine ridge with zero slope at trough and crest.
		@param t
			Position; the profile repeats with period 1
		@param asymmetry
			Share of the period spent climbing, clamped to 0.05..0.95
		@return
			Height in [0, 1]
		*/
	double duneProfile(double t, double asymmetry)
	{
		constexpr double kPi = 3.14159265358979323846;

		const double a = std::clamp(asymmetry, 0.05, 0.95);
		const double f = t - std::floor(t);
		const double u = f < a ? f / a : (1.0 - f) / (1.0 - a);
		return 0.5 - 0.5 * std::cos(kPi * u);
	}

	/*!
		@brief
			Folds a height into flat treads joined by smoothstep risers; returns h unchanged if step <= 0.
		@param h
			Height to fold
		@param step
			Height of one step
		@param sharpness
			1 gives a gentle S per step; larger narrows the riser (at least 1 is used)
		@return
			The terraced height
		*/
	double terrace(double h, double step, double sharpness)
	{
		if (!(step > 0.0))
			return h;

		const double scaled = h / step;
		const double tread = std::floor(scaled);
		const double f = scaled - tread;
		const double riser = std::clamp((f - 0.5) * std::max(1.0, sharpness) + 0.5, 0.0, 1.0);
		return (tread + riser * riser * (3.0 - 2.0 * riser)) * step;
	}

	/*!
		@brief
			Offsets (x, y) by two independent fbm fields (seeded from "warp.x" / "warp.y") scaled by amplitude.
		@param seed
			Field seed
		@param x
			Input x
		@param y
			Input y
		@param amplitude
			Offset scale in input units
		@param p
			Fractal parameters of the warp fields
		@param outX
			Receives the warped x
		@param outY
			Receives the warped y
		*/
	void domainWarp2(std::uint64_t seed, double x, double y, double amplitude, const FbmParams& p,
					 double& outX, double& outY)
	{
		outX = x + amplitude * fbm2(seed ^ hashString("warp.x"), x, y, p);
		outY = y + amplitude * fbm2(seed ^ hashString("warp.y"), x, y, p);
	}
}
