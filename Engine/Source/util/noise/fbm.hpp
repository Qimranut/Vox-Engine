/*************************************************************************************************************
\file       fbm.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Fractal noise API over simplex noise (fbm, ridged, damped, billow) and the shaping helpers
            (smoothstep, smoothAbs, dune profile, terrace, domain warp) the terrain generator builds on.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// stds
#include <cstdint>


namespace util::noise
{
	/**
	 * Fractal parameters, as designers write them in a recipe. Frequency is
	 * per input unit (per voxel for terrain).
	 *
	 */
	struct FbmParams
	{
		int		octaves_	{ 4 };
		double	frequency_	{ 0.01 };
		double	lacunarity_	{ 2.0 };
		double	gain_		{ 0.5 };

		bool operator==(const FbmParams&) const = default;
	};

	/*!
		@brief
			Fractional Brownian motion: octaves of simplex noise summed with decaying gain.

			Normalised to roughly [-1, 1].
		@param seed
			Field seed; derive one per named field so fields decorrelate
		@param x
			Sample x in input units
		@param y
			Sample y in input units
		@param p
			Fractal parameters
		@return
			The noise value
		*/
	double fbm2(std::uint64_t seed, double x, double y, const FbmParams& p);

	/*!
		@brief
			3D fractional Brownian motion, normalised to roughly [-1, 1].
		@param seed
			Field seed
		@param x
			Sample x in input units
		@param y
			Sample y in input units
		@param z
			Sample z in input units
		@param p
			Fractal parameters
		@return
			The noise value
		*/
	double fbm3(std::uint64_t seed, double x, double y, double z, const FbmParams& p);

	/*!
		@brief
			Ridged multifractal (Musgrave): sharp crests where the noise crosses zero.

			Each octave is weighted by the previous one so ridges stay coherent.
		@param seed
			Field seed
		@param x
			Sample x in input units
		@param y
			Sample y in input units
		@param p
			Fractal parameters
		@return
			Ridge value in [0, 1]
		*/
	double ridged2(std::uint64_t seed, double x, double y, const FbmParams& p);

	/*!
		@brief
			ridged2 with the crest rounded off: |n| becomes sqrt(n^2 + eps).

			A ridge line is a smooth cap rather than a crease. A crease sampled
			on a voxel grid reads as a row of one-voxel spikes.
		@param seed
			Field seed
		@param x
			Sample x in input units
		@param y
			Sample y in input units
		@param p
			Fractal parameters
		@return
			Ridge value in [0, 1]
		*/
	double ridgedSmooth2(std::uint64_t seed, double x, double y, const FbmParams& p);

	/*!
		@brief
			fbm whose octaves are damped by the slope accumulated so far (Quilez, "more noise").

			Each octave is divided by 1 + damping * |gradient|^2. Steep ground
			loses its fine detail and flat ground keeps it, which reads as
			eroded slopes instead of uniform fuzz.
		@param seed
			Field seed
		@param x
			Sample x in input units
		@param y
			Sample y in input units
		@param p
			Fractal parameters
		@param damping
			0 is plain fbm; larger is smoother
		@return
			The noise value, roughly [-1, 1]
		*/
	double dampedFbm2(std::uint64_t seed, double x, double y, const FbmParams& p, double damping);

	/*!
		@brief
			Hermite step, clamped; e1 must differ from e0.
		@param e0
			Edge where the result is 0
		@param e1
			Edge where the result is 1
		@param x
			Input value
		@return
			Smoothly interpolated value in [0, 1]
		*/
	inline double smoothstep(double e0, double e1, double x)
	{
		double t = (x - e0) / (e1 - e0);
		t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
		return t * t * (3.0 - 2.0 * t);
	}

	/*!
		@brief
			|n| with the corner at zero rounded off: sqrt(n^2 + eps).

			A crease sampled on a voxel grid reads as a row of one-voxel steps.
		@param n
			Input value
		@param eps
			Rounding amount at zero
		@return
			The smoothed absolute value
		*/
	double smoothAbs(double n, double eps = 0.004);

	/*!
		@brief
			Billowed fbm: octaves of 2|n| - 1, giving rounded pillows with soft creases between them.

			Suits mounds, hummocks and drifts. The fold uses smoothAbs, so the
			creases are rounded too.
		@param seed
			Field seed
		@param x
			Sample x in input units
		@param y
			Sample y in input units
		@param p
			Fractal parameters
		@return
			The noise value, roughly [-1, 1]
		*/
	double billow2(std::uint64_t seed, double x, double y, const FbmParams& p);

	/*!
		@brief
			One period of an asymmetric ridge, period 1 in t.

			Rises over the first asymmetry of the period and falls over the
			rest, with zero slope at the trough and at the crest so neighbouring
			periods join without a crease.
		@param t
			Position along the profile
		@param asymmetry
			Share of the period spent climbing, clamped to 0.05..0.95; above 0.5
			gives a long windward slope and a short steep lee
		@return
			Height in [0, 1]
		*/
	double duneProfile(double t, double asymmetry);

	/*!
		@brief
			Folds a height into steps of the given size: flat treads joined by smooth risers.

			Continuous and never decreasing in h.
		@param h
			Height to fold
		@param step
			Size of one step
		@param sharpness
			1 leaves a gentle S per step; larger makes the riser a narrower
			share of the step (the riser covers 1 / sharpness of it)
		@return
			The terraced height
		*/
	double terrace(double h, double step, double sharpness);

	/*!
		@brief
			Domain warp: offsets (x, y) by two independent fbm fields scaled by amplitude.

			Turns regular noise into flowing, organic shapes.
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
					 double& outX, double& outY);
}
