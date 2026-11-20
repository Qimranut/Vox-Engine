/*************************************************************************************************************
\file       texture_self_check.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares the startup texture self-check ([texture-check]), which verifies uploads, the DDS
            and .vmdl parsers, BCn decoding, TextureManager and samplers against the real driver.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once


namespace vox
{
	class ResourceManager;
}

namespace graphics::texture
{
	/*!
		@brief
			Verifies the texture path against the real driver at startup.

			Row order, sRGB storage, mip counts, the DDS parser, BC1 decoding,
			fallbacks, stale handles and sampler sharing - the things that go wrong
			as a wrong-looking image rather than a crash. Uses in-memory images and
			a throwaway TextureManager, so it needs no asset files and leaves the
			real manager untouched. Reports to stdout/stderr; never throws.
			Needs a current OpenGL 4.5 context.
		@param resources
			Used only to prove a missing file falls back cleanly
		@return
			True if every check passed
		*/
	bool runSelfCheck(vox::ResourceManager& resources);
}
