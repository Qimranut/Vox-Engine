/*************************************************************************************************************
\file       texture_types.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      GL-free vocabulary for textures and samplers: colour space, filters, wrap modes, SamplerDesc,
            TextureHandle and texture units. Anything that only needs to name a texture - components,
            draw lists, gameplay code - includes this and never GLEW.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// stds
#include <cstddef>
#include <cstdint>
#include <functional>


namespace graphics::texture
{
	/**
	 * How the texel values of an image are encoded.
	 *
	 * Srgb is for colour a person authored by eye: albedo, emissive, UI. The
	 * GPU decodes it to linear on every sample, before filtering. Linear is for
	 * data: normals, roughness, masks, HDR.
	 *
	 */
	enum class ColorSpace : std::uint8_t
	{
		Linear,
		Srgb
	};

	enum class Filter : std::uint8_t
	{
		Nearest,
		Linear
	};

	/**
	 * Filtering between mip levels. None samples only the base level.
	 *
	 */
	enum class MipFilter : std::uint8_t
	{
		None,
		Nearest,
		Linear
	};

	enum class Wrap : std::uint8_t
	{
		Repeat,
		MirroredRepeat,
		ClampToEdge
	};

	/**
	 * Everything a sampler object holds. Texture and sampling state are kept
	 * apart so one texture can be read tiled in one place and clamped in
	 * another without a second copy.
	 *
	 */
	struct SamplerDesc
	{
		Filter		minFilter_		{ Filter::Linear };
		Filter		magFilter_		{ Filter::Linear };
		MipFilter	mipFilter_		{ MipFilter::Linear };
		Wrap		wrapU_			{ Wrap::Repeat };
		Wrap		wrapV_			{ Wrap::Repeat };

		// 1 disables anisotropic filtering. Clamped to what the driver
		// supports when the sampler is created.
		float		anisotropy_		{ 8.f };

		bool operator==(const SamplerDesc&) const = default;

		/*!
			@brief
				Trilinear, repeating, 8x anisotropic. The default for surfaces.
			@return
				The trilinear repeat description
			*/
		static SamplerDesc trilinearRepeat() { return SamplerDesc{}; }

		/*!
			@brief
				Unfiltered and clamped, for palette lookups and pixel art where
				blending neighbouring texels would invent colours.
			@return
				The point clamp description
			*/
		static SamplerDesc pointClamp()
		{
			return SamplerDesc{ Filter::Nearest, Filter::Nearest, MipFilter::None,
				Wrap::ClampToEdge, Wrap::ClampToEdge, 1.f };
		}
	};

	struct SamplerDescHash
	{
		/*!
			@brief
				Hashes every field of a sampler description.
			@param d
				Description to hash
			@return
				The combined hash
			*/
		std::size_t operator()(const SamplerDesc& d) const noexcept
		{
			std::size_t h = std::hash<float>{}(d.anisotropy_);
			const auto mix = [&h](std::size_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); };

			mix(static_cast<std::size_t>(d.minFilter_));
			mix(static_cast<std::size_t>(d.magFilter_));
			mix(static_cast<std::size_t>(d.mipFilter_));
			mix(static_cast<std::size_t>(d.wrapU_));
			mix(static_cast<std::size_t>(d.wrapV_));

			return h;
		}
	};

	/**
	 * Generational handle to a texture owned by TextureManager.
	 *
	 * A handle outlives the texture it names safely: once the slot is freed or
	 * reused, the generation no longer matches and the manager resolves the
	 * handle to its missing-texture fallback instead of to whatever moved in.
	 * Generation 0 is never issued, so a default handle is always invalid.
	 *
	 */
	struct TextureHandle
	{
		std::uint32_t	index_			{ 0 };
		std::uint32_t	generation_		{ 0 };

		/*!
			@brief
				Whether the handle was ever issued; says nothing about whether it is still alive.
			@return
				False for a default-constructed handle
			*/
		bool isValid() const { return generation_ != 0; }

		bool operator==(const TextureHandle&) const = default;

		/*!
			@brief
				Orders handles by slot, then generation.
			@param o
				Handle to compare against
			@return
				Order by slot, for sorting draws so equal textures are adjacent
			*/
		bool operator<(const TextureHandle& o) const
		{
			return index_ != o.index_ ? index_ < o.index_ : generation_ < o.generation_;
		}
	};

	/**
	 * Fixed texture units. The shaders mirror these with
	 * layout(binding = N), so C++ never sets sampler uniforms.
	 *
	 */
	enum class TextureUnit : std::uint32_t
	{
		Albedo = 0,
		Skybox = 1,
		Irradiance = 2,
		Prefiltered = 3,
		BrdfLut = 4,
		Normal = 5
	};
}
