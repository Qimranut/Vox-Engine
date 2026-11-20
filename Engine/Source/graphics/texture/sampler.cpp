/*************************************************************************************************************
\file       sampler.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      GL sampler objects built from a SamplerDesc, and the cache that shares one sampler per
            distinct (driver-clamped) description.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "sampler.hpp"

// stds
#include <algorithm>


namespace graphics::texture
{
	namespace
	{
		/*!
			@brief
				Whether the driver supports anisotropic filtering.

				Core in 4.6; an extension under either name before that.
			@return
				True when anisotropic filtering can be set
			*/
		bool hasAnisotropy()
		{
			return GLEW_VERSION_4_6 || GLEW_ARB_texture_filter_anisotropic || GLEW_EXT_texture_filter_anisotropic;
		}

		/*!
			@brief
				Maps a Wrap mode to its GL enum.
			@param wrap
				Wrap mode to convert
			@return
				The GL wrap enum, GL_REPEAT by default
			*/
		GLint toGl(Wrap wrap)
		{
			switch (wrap)
			{
			case Wrap::MirroredRepeat:	return GL_MIRRORED_REPEAT;
			case Wrap::ClampToEdge:		return GL_CLAMP_TO_EDGE;
			case Wrap::Repeat:
			default:					return GL_REPEAT;
			}
		}

		/*!
			@brief
				Combines a minification filter and a mip filter into the GL min-filter enum.
			@param filter
				Filter within a mip level
			@param mip
				Filter between mip levels (None for no mipmapping)
			@return
				The GL minification filter enum
			*/
		GLint minFilterToGl(Filter filter, MipFilter mip)
		{
			const bool linear = filter == Filter::Linear;

			switch (mip)
			{
			case MipFilter::Nearest:	return linear ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST;
			case MipFilter::Linear:		return linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR;
			case MipFilter::None:
			default:					return linear ? GL_LINEAR : GL_NEAREST;
			}
		}
	}

	/*!
		@brief
			Creates the GL sampler and sets filtering, wrapping and (when supported) anisotropy.
		@param desc
			Sampling description; anisotropy is used as given
		*/
	Sampler::Sampler(const SamplerDesc& desc)
		:
		desc_{ desc }
	{
		glCreateSamplers(1, &id_);

		glSamplerParameteri(id_, GL_TEXTURE_MIN_FILTER, minFilterToGl(desc.minFilter_, desc.mipFilter_));
		glSamplerParameteri(id_, GL_TEXTURE_MAG_FILTER, desc.magFilter_ == Filter::Linear ? GL_LINEAR : GL_NEAREST);
		glSamplerParameteri(id_, GL_TEXTURE_WRAP_S, toGl(desc.wrapU_));
		glSamplerParameteri(id_, GL_TEXTURE_WRAP_T, toGl(desc.wrapV_));

		// Setting it without driver support is GL_INVALID_ENUM.
		if (hasAnisotropy())
			glSamplerParameterf(id_, GL_TEXTURE_MAX_ANISOTROPY, std::max(desc.anisotropy_, 1.f));
	}

	/*!
		@brief
			Deletes the GL sampler if this object still owns one.
		*/
	Sampler::~Sampler()
	{
		if (id_ != 0)
			glDeleteSamplers(1, &id_);
	}

	/*!
		@brief
			Takes over another sampler's GL object, leaving it empty.
		@param other
			Sampler to move from
		*/
	Sampler::Sampler(Sampler&& other) noexcept
		:
		id_		{ other.id_ },
		desc_	{ other.desc_ }
	{
		other.id_ = 0;
	}

	/*!
		@brief
			Releases this sampler's GL object and takes over another's.
		@param other
			Sampler to move from
		@return
			This sampler
		*/
	Sampler& Sampler::operator=(Sampler&& other) noexcept
	{
		if (this != &other)
		{
			if (id_ != 0)
				glDeleteSamplers(1, &id_);

			id_		= other.id_;
			desc_	= other.desc_;

			other.id_ = 0;
		}

		return *this;
	}

	/*!
		@brief
			Binds this sampler to a texture unit.
		@param unit
			Texture unit to bind to
		*/
	void Sampler::bind(TextureUnit unit) const
	{
		glBindSampler(static_cast<GLuint>(unit), id_);
	}

	/*!
		@brief
			Clears whatever sampler is on a unit, back to texture-owned state.
		@param unit
			Texture unit to clear
		*/
	void Sampler::unbind(TextureUnit unit)
	{
		glBindSampler(static_cast<GLuint>(unit), 0);
	}

	/*!
		@brief
			Reads the driver's maximum anisotropy. Needs a current context.
		*/
	SamplerCache::SamplerCache()
	{
		if (hasAnisotropy())
		{
			GLfloat max = 1.f;
			glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &max);
			maxAnisotropy_ = std::max(max, 1.f);
		}
	}

	/*!
		@brief
			Returns the shared sampler for a description, creating it on first use.

			Anisotropy is clamped before the lookup so descriptions that end up
			identical on this driver share one sampler.
		@param desc
			Sampling description
		@return
			The shared sampler; the reference stays valid for the cache's lifetime
		*/
	const Sampler& SamplerCache::get(SamplerDesc desc)
	{
		// Clamp before the lookup so descriptions that end up identical on
		// this driver share one sampler. Anisotropy only means anything on a
		// filtered, mipmapped minification.
		const bool filtered = desc.minFilter_ == Filter::Linear && desc.mipFilter_ != MipFilter::None;
		desc.anisotropy_ = filtered ? std::clamp(desc.anisotropy_, 1.f, maxAnisotropy_) : 1.f;

		auto it = samplers_.find(desc);

		if (it == samplers_.end())
			it = samplers_.emplace(desc, Sampler{ desc }).first;

		return it->second;
	}
}
