/*************************************************************************************************************
\file       simplex_noise.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Seeded 2D and 3D simplex noise (Gustavson) with hashed lattice gradients, plus 2D noise
            with its analytic gradient.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "simplex_noise.hpp"

// Noise
#include "hash.hpp"

// stds
#include <cmath>


namespace util::noise
{
	namespace
	{
		// The 12 edge-midpoint directions of a cube. Used for both 2D (x, y
		// components) and 3D, as in the reference implementation.
		constexpr int kGrad3[12][3] =
		{
			{ 1, 1, 0 }, { -1, 1, 0 }, { 1, -1, 0 }, { -1, -1, 0 },
			{ 1, 0, 1 }, { -1, 0, 1 }, { 1, 0, -1 }, { -1, 0, -1 },
			{ 0, 1, 1 }, { 0, -1, 1 }, { 0, 1, -1 }, { 0, -1, -1 }
		};

		/*!
			@brief
				Picks one of the 12 gradients for a lattice corner by hashing it with the seed.
			@param seed
				Field seed
			@param x
				Lattice x
			@param y
				Lattice y
			@param z
				Lattice z
			@return
				Index into kGrad3
			*/
		inline int gradientIndex(std::uint64_t seed, std::int64_t x, std::int64_t y, std::int64_t z)
		{
			return static_cast<int>(hashCoords(seed, x, y, z) % 12u);
		}

		/*!
			@brief
				Dot product of a gradient's x, y components with (x, y).
			@param g
				Gradient (at least 2 components)
			@param x
				Offset x
			@param y
				Offset y
			@return
				The dot product
			*/
		inline double dot2(const int* g, double x, double y)
		{
			return g[0] * x + g[1] * y;
		}

		/*!
			@brief
				Dot product of a 3-component gradient with (x, y, z).
			@param g
				Gradient (3 components)
			@param x
				Offset x
			@param y
				Offset y
			@param z
				Offset z
			@return
				The dot product
			*/
		inline double dot3(const int* g, double x, double y, double z)
		{
			return g[0] * x + g[1] * y + g[2] * z;
		}

		/*!
			@brief
				Floor of a double as a 64-bit integer, without calling std::floor.
			@param v
				Value to floor
			@return
				The largest integer not greater than v
			*/
		inline std::int64_t fastFloor(double v)
		{
			const std::int64_t i = static_cast<std::int64_t>(v);
			return v < i ? i - 1 : i;
		}
	}

	/*!
		@brief
			2D simplex noise: sums the three simplex corner contributions.
		@param seed
			Field seed
		@param x
			Sample x (already scaled by frequency)
		@param y
			Sample y (already scaled by frequency)
		@return
			Noise value in roughly [-1, 1]
		*/
	double noise2(std::uint64_t seed, double x, double y)
	{
		constexpr double F2 = 0.36602540378443864676;   // 0.5 * (sqrt(3) - 1)
		constexpr double G2 = 0.21132486540518711775;   // (3 - sqrt(3)) / 6

		const double s = (x + y) * F2;
		const std::int64_t i = fastFloor(x + s);
		const std::int64_t j = fastFloor(y + s);

		const double t = static_cast<double>(i + j) * G2;
		const double x0 = x - (static_cast<double>(i) - t);
		const double y0 = y - (static_cast<double>(j) - t);

		const int i1 = x0 > y0 ? 1 : 0;
		const int j1 = 1 - i1;

		const double x1 = x0 - i1 + G2;
		const double y1 = y0 - j1 + G2;
		const double x2 = x0 - 1.0 + 2.0 * G2;
		const double y2 = y0 - 1.0 + 2.0 * G2;

		double n = 0.0;

		double t0 = 0.5 - x0 * x0 - y0 * y0;
		if (t0 > 0.0)
		{
			t0 *= t0;
			n += t0 * t0 * dot2(kGrad3[gradientIndex(seed, i, j, 0)], x0, y0);
		}

		double t1 = 0.5 - x1 * x1 - y1 * y1;
		if (t1 > 0.0)
		{
			t1 *= t1;
			n += t1 * t1 * dot2(kGrad3[gradientIndex(seed, i + i1, j + j1, 0)], x1, y1);
		}

		double t2 = 0.5 - x2 * x2 - y2 * y2;
		if (t2 > 0.0)
		{
			t2 *= t2;
			n += t2 * t2 * dot2(kGrad3[gradientIndex(seed, i + 1, j + 1, 0)], x2, y2);
		}

		return 70.0 * n;
	}

	/*!
		@brief
			2D simplex noise together with its analytic gradient.
		@param seed
			Field seed
		@param x
			Sample x (already scaled by frequency)
		@param y
			Sample y (already scaled by frequency)
		@param dx
			Receives the partial derivative in x
		@param dy
			Receives the partial derivative in y
		@return
			The same value noise2 returns for these inputs
		*/
	double noise2Deriv(std::uint64_t seed, double x, double y, double& dx, double& dy)
	{
		constexpr double F2 = 0.36602540378443864676;
		constexpr double G2 = 0.21132486540518711775;

		const double s = (x + y) * F2;
		const std::int64_t i = fastFloor(x + s);
		const std::int64_t j = fastFloor(y + s);

		const double t = static_cast<double>(i + j) * G2;
		const double x0 = x - (static_cast<double>(i) - t);
		const double y0 = y - (static_cast<double>(j) - t);

		const int i1 = x0 > y0 ? 1 : 0;
		const int j1 = 1 - i1;

		const double x1 = x0 - i1 + G2;
		const double y1 = y0 - j1 + G2;
		const double x2 = x0 - 1.0 + 2.0 * G2;
		const double y2 = y0 - 1.0 + 2.0 * G2;

		double n = 0.0;
		dx = 0.0;
		dy = 0.0;

		// One corner: value t^4 (g . d), gradient t^4 g - 8 t^3 (g . d) d.
		const auto corner = [&](std::int64_t ci, std::int64_t cj, double cx, double cy)
		{
			const double tc = 0.5 - cx * cx - cy * cy;
			if (tc <= 0.0)
				return;

			const int* g = kGrad3[gradientIndex(seed, ci, cj, 0)];
			const double gd = dot2(g, cx, cy);
			const double t2 = tc * tc;
			const double t4 = t2 * t2;

			n += t4 * gd;
			dx += t4 * g[0] - 8.0 * t2 * tc * gd * cx;
			dy += t4 * g[1] - 8.0 * t2 * tc * gd * cy;
		};

		corner(i, j, x0, y0);
		corner(i + i1, j + j1, x1, y1);
		corner(i + 1, j + 1, x2, y2);

		dx *= 70.0;
		dy *= 70.0;
		return 70.0 * n;
	}

	/*!
		@brief
			3D simplex noise: sums the four simplex corner contributions.
		@param seed
			Field seed
		@param x
			Sample x (already scaled by frequency)
		@param y
			Sample y (already scaled by frequency)
		@param z
			Sample z (already scaled by frequency)
		@return
			Noise value in roughly [-1, 1]
		*/
	double noise3(std::uint64_t seed, double x, double y, double z)
	{
		constexpr double F3 = 1.0 / 3.0;
		constexpr double G3 = 1.0 / 6.0;

		const double s = (x + y + z) * F3;
		const std::int64_t i = fastFloor(x + s);
		const std::int64_t j = fastFloor(y + s);
		const std::int64_t k = fastFloor(z + s);

		const double t = static_cast<double>(i + j + k) * G3;
		const double x0 = x - (static_cast<double>(i) - t);
		const double y0 = y - (static_cast<double>(j) - t);
		const double z0 = z - (static_cast<double>(k) - t);

		int i1, j1, k1, i2, j2, k2;
		if (x0 >= y0)
		{
			if (y0 >= z0)		{ i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
			else if (x0 >= z0)	{ i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 0; k2 = 1; }
			else				{ i1 = 0; j1 = 0; k1 = 1; i2 = 1; j2 = 0; k2 = 1; }
		}
		else
		{
			if (y0 < z0)		{ i1 = 0; j1 = 0; k1 = 1; i2 = 0; j2 = 1; k2 = 1; }
			else if (x0 < z0)	{ i1 = 0; j1 = 1; k1 = 0; i2 = 0; j2 = 1; k2 = 1; }
			else				{ i1 = 0; j1 = 1; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
		}

		const double x1 = x0 - i1 + G3;
		const double y1 = y0 - j1 + G3;
		const double z1 = z0 - k1 + G3;
		const double x2 = x0 - i2 + 2.0 * G3;
		const double y2 = y0 - j2 + 2.0 * G3;
		const double z2 = z0 - k2 + 2.0 * G3;
		const double x3 = x0 - 1.0 + 3.0 * G3;
		const double y3 = y0 - 1.0 + 3.0 * G3;
		const double z3 = z0 - 1.0 + 3.0 * G3;

		double n = 0.0;

		double t0 = 0.6 - x0 * x0 - y0 * y0 - z0 * z0;
		if (t0 > 0.0)
		{
			t0 *= t0;
			n += t0 * t0 * dot3(kGrad3[gradientIndex(seed, i, j, k)], x0, y0, z0);
		}

		double t1 = 0.6 - x1 * x1 - y1 * y1 - z1 * z1;
		if (t1 > 0.0)
		{
			t1 *= t1;
			n += t1 * t1 * dot3(kGrad3[gradientIndex(seed, i + i1, j + j1, k + k1)], x1, y1, z1);
		}

		double t2 = 0.6 - x2 * x2 - y2 * y2 - z2 * z2;
		if (t2 > 0.0)
		{
			t2 *= t2;
			n += t2 * t2 * dot3(kGrad3[gradientIndex(seed, i + i2, j + j2, k + k2)], x2, y2, z2);
		}

		double t3 = 0.6 - x3 * x3 - y3 * y3 - z3 * z3;
		if (t3 > 0.0)
		{
			t3 *= t3;
			n += t3 * t3 * dot3(kGrad3[gradientIndex(seed, i + 1, j + 1, k + 1)], x3, y3, z3);
		}

		return 32.0 * n;
	}
}
