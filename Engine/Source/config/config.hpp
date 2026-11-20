#pragma once

// Config
#include "cfg_params/params.hpp"

/* -----------------------------------------------------*/
/*						Libs in use						*/
/*					   --------------					*/
				#define VOX_GLFW_ACTIVE 1
				#define VOX_FREETYPE_ACTIVE 1
/* -----------------------------------------------------*/


namespace cfg
{
	struct Settings
	{
		params::Window windowDim{ 200, 200, 1280, 720 };
	};

	inline Settings set;
}
