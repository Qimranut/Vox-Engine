/*************************************************************************************************************
\file       spline.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Piecewise-linear Spline: sorts its points on construction and evaluates by binary search,
            clamped outside the first and last point.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "spline.hpp"

// stds
#include <algorithm>


namespace util::noise
{
	/*!
		@brief
			Takes the points and sorts them by input.
		@param points
			Control points (input, output)
		*/
	Spline::Spline(std::vector<Point> points)
		: points_(std::move(points))
	{
		std::sort(points_.begin(), points_.end(),
			[](const Point& a, const Point& b) { return a.first < b.first; });
	}

	/*!
		@brief
			Linearly interpolates the curve at t, clamped to the end points; 0 when empty.
		@param t
			Input value
		@return
			The curve's output at t
		*/
	float Spline::eval(float t) const
	{
		if (points_.empty())
			return 0.f;

		if (t <= points_.front().first)
			return points_.front().second;

		if (t >= points_.back().first)
			return points_.back().second;

		// First point strictly past t; the segment is [it-1, it].
		const auto it = std::upper_bound(points_.begin(), points_.end(), t,
			[](float value, const Point& p) { return value < p.first; });

		const Point& a = *(it - 1);
		const Point& b = *it;

		const float span = b.first - a.first;
		if (span <= 0.f)
			return b.second;

		const float u = (t - a.first) / span;
		return a.second + (b.second - a.second) * u;
	}
}
