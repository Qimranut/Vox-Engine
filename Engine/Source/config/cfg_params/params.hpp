#pragma once

// stds
#include <utility>


namespace cfg::params
{
	struct Window
	{
		Window(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
			:
			x_{ x },
			y_{ y },
			w_{ w },
			h_{ h }
		{}

		// Pos
		uint16_t x_;
		uint16_t y_;

		// Size
		uint16_t w_;
		uint16_t h_;
	};
}
