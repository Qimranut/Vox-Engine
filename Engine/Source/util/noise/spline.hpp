/*************************************************************************************************************
\file       spline.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Spline, a piecewise-linear curve a recipe uses to map a noise value to a height.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// stds
#include <utility>
#include <vector>


namespace util::noise
{
	/**
	 * Piecewise-linear curve through sorted (input, output) points, clamped
	 * outside the first and last point. The way a recipe maps a noise value
	 * in [-1, 1] to a height in voxels.
	 *
	 */
	class Spline
	{
	public:

		using Point = std::pair<float, float>;

		Spline() = default;

		/*!
			@brief
				Builds the curve from its control points, sorting them by input.
			@param points
				Sorted by input; an empty spline evaluates to 0
			*/
		explicit Spline(std::vector<Point> points);

		/*!
			@brief
				Evaluates the curve at t, clamped outside the end points.
			@param t
				Input value
			@return
				The interpolated output (0 for an empty spline)
			*/
		float eval(float t) const;

		/*!
			@brief
				Whether the spline has no points.
			@return
				True when there are no points
			*/
		bool empty() const { return points_.empty(); }

		/*!
			@brief
				The sorted control points.
			@return
				The points
			*/
		const std::vector<Point>& points() const { return points_; }

	private:

		std::vector<Point> points_;
	};
}
