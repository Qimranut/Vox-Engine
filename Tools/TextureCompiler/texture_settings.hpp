/*************************************************************************************************************
\file       texture_settings.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Import settings for one source image (kind, albedo format, mips, green flip, alpha cutoff) and
            how they are resolved from a .texmeta sidecar, the file name suffix and the extension.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// stds
#include <filesystem>


// Import settings for one source image: what the texels mean, which decides
// the block-compressed format it is cooked to. DirectXTex-free on purpose.
namespace tools::texture
{
	/**
	 * What a texture holds. Each kind maps to the BCn format built for it:
	 *
	 *   Albedo  colour a person authored by eye  BC7 sRGB (or BC1 / BC3 sRGB)
	 *   Normal  tangent-space normal map         BC5, X and Y only
	 *   Mask    one channel: rough/metal/AO/SDF  BC4
	 *   Hdr     high dynamic range light         BC6H unsigned float
	 *
	 */
	enum class TextureKind
	{
		Albedo,
		Normal,
		Mask,
		Hdr
	};

	/**
	 * Albedo only. BC7 is the default: 8bpp, the best quality for RGB and
	 * RGBA. BC1 halves that to 4bpp with 1-bit alpha, for large opaque
	 * surfaces where the size matters more; BC3 is 8bpp with smooth alpha,
	 * for compatibility.
	 *
	 */
	enum class AlbedoFormat
	{
		BC7,
		BC1,
		BC3
	};

	struct TextureSettings
	{
		TextureKind		kind_			{ TextureKind::Albedo };
		AlbedoFormat	albedoFormat_	{ AlbedoFormat::BC7 };

		// Generate the full mip chain. Off only for textures never minified, like UI.
		bool			mips_			{ true };

		// Normal maps authored DirectX-style (-Y). The engine uses OpenGL +Y.
		bool			flipGreen_		{ false };

		// Alpha-test reference in (0, 1]. When set, mips keep the base level's
		// alpha coverage so cutouts do not thin out with distance. 0 = off.
		float			alphaCutoff_	{ 0.f };
	};

	/*!
		@brief
			Names a texture kind for logs and error messages.
		@param kind
			The texture kind
		@return
			"albedo", "normal", "mask" or "hdr"
		*/
	const char* kindName(TextureKind kind);

	/*!
		@brief
			Checks whether a file is an image the compiler cooks.
		@param path
			The file to inspect
		@return
			True for png, jpg, jpeg, bmp, tga and hdr, the same set vox::ResourceManager decodes
		*/
	bool isSourceImage(const std::filesystem::path& path);

	/*!
		@brief
			Returns the settings sidecar path for a source image.
		@param source
			The source image
		@return
			<source>.texmeta, the optional sidecar holding this image's settings
		*/
	std::filesystem::path sidecarPath(const std::filesystem::path& source);

	/*!
		@brief
			Resolves a source image's settings, first match wins.

			1. <source>.texmeta, "key = value" lines, '#' comments:
			   kind = albedo | normal | mask | hdr
			   format = bc7 | bc1 | bc3        (albedo only)
			   mips = true | false
			   flip_green = true | false        (normal only)
			   alpha_cutoff = 0.5               (albedo only)
			2. File name suffix: _n, _normal -> Normal; _mask, _r, _m, _ao, _h, _sdf -> Mask
			3. .hdr -> Hdr
			4. Albedo

			A sidecar overrides only the keys it names: without a kind line, the kind still comes from the
			suffix or extension. Throws std::runtime_error on an unreadable or malformed sidecar, so a typo
			never silently cooks the wrong format.
		@param source
			The source image
		@return
			The settings
		*/
	TextureSettings resolveSettings(const std::filesystem::path& source);
}
