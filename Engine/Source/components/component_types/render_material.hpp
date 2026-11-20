/*************************************************************************************************************
\file       render_material.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      cpn::RenderMaterial, the surface appearance of a drawn entity, held as asset paths only
            (currently the albedo texture).

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// stds
#include <string>


namespace cpn
{
	/**
	 * Surface appearance of a drawn entity.
	 *
	 * Holds asset paths only, never GPU handles: the renderer resolves the
	 * path to a texture when it draws, so this stays serialisable and
	 * GL-free (comp_list.hpp reaches nearly every translation unit).
	 *
	 */
	struct RenderMaterial
	{
		// Colour texture, relative to the asset root, sampled as sRGB and
		// multiplied by Visibility::tint_. Empty draws untextured.
		std::string albedo_;
		std::string normalMap_;
		float metallic_{ 1.0f };
		float roughness_{ 0.3f };
		float ao_{ 1.0f };
	};
}
