/*************************************************************************************************************
\file       sampler.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      GL sampler objects (filtering, wrapping, anisotropy) kept separate from textures, and
            SamplerCache, which shares one sampler per distinct description.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Texture
#include "texture_types.hpp"

// GLEW
#include <GL/glew.h>

// stds
#include <unordered_map>


namespace graphics::texture
{
	/**
	 * A GL sampler object: filtering, wrapping and anisotropy, independent of
	 * any texture. Bound to a unit, it overrides the sampling state of
	 * whatever texture sits on that unit.
	 *
	 * GL thread only.
	 *
	 */
	class Sampler
	{
	public:

		/*!
			@brief
				Creates the GL sampler from a description.
			@param desc
				Anisotropy is used as given; SamplerCache clamps it first
			*/
		explicit Sampler(const SamplerDesc& desc);

		/*!
			@brief
				Deletes the GL sampler if still owned.
			*/
		~Sampler();

		// NO COPY
		Sampler(const Sampler&) = delete;
		Sampler& operator=(const Sampler&) = delete;

		// YES MOVE
		/*!
			@brief
				Takes over another sampler's GL object, leaving it empty.
			@param other
				Sampler to move from
			*/
		Sampler(Sampler&& other) noexcept;

		/*!
			@brief
				Releases this sampler's GL object and takes over another's.
			@param other
				Sampler to move from
			@return
				This sampler
			*/
		Sampler& operator=(Sampler&& other) noexcept;

		/*!
			@brief
				Binds this sampler to a texture unit.
			@param unit
				Texture unit to bind to
			*/
		void bind(TextureUnit unit) const;

		/*!
			@brief
				Clears whatever sampler is on a unit, back to texture-owned state.
			@param unit
				Texture unit to clear
			*/
		static void unbind(TextureUnit unit);

		/*!
			@brief
				Gets the GL sampler name.
			@return
				The GL sampler id, 0 when moved from
			*/
		GLuint				id() const		{ return id_; }

		/*!
			@brief
				Gets the description the sampler was built from.
			@return
				The sampler's description
			*/
		const SamplerDesc&	desc() const	{ return desc_; }

	private:

		GLuint		id_		{ 0 };
		SamplerDesc	desc_	{};
	};

	/**
	 * Owns one sampler per distinct description, so equal descriptions share
	 * a GL object.
	 *
	 */
	class SamplerCache
	{
	public:

		/*!
			@brief
				Reads the driver's anisotropy support. Needs a current context.
			*/
		SamplerCache();

		/*!
			@brief
				Returns the shared sampler for a description, creating it on first use.
			@param desc
				Sampling description; anisotropy is clamped to what the driver supports
			@return
				The shared sampler. The reference stays valid for the cache's
				lifetime.
			*/
		const Sampler& get(SamplerDesc desc);

		/*!
			@brief
				Gets the driver's anisotropy limit.
			@return
				Largest supported anisotropy, 1 when the driver has none
			*/
		float maxAnisotropy() const { return maxAnisotropy_; }

	private:

		float maxAnisotropy_ { 1.f };

		// Node-based, so references handed out survive rehashing.
		std::unordered_map<SamplerDesc, Sampler, SamplerDescHash> samplers_;
	};
}
