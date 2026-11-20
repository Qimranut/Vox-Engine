/*************************************************************************************************************
\file       texture.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Texture2D, an immutable, move-only 2D GPU texture created with direct state access from a
            cooked texture resource or raw uncompressed pixels.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Texture
#include "texture_types.hpp"

// Resources
#include "resources/ResourceManager.hpp"

// GLEW
#include <GL/glew.h>

// stds
#include <span>
#include <string_view>


namespace graphics::texture
{
	/**
	 * An immutable 2D texture on the GPU, created with direct state access.
	 *
	 * Storage is allocated once with glTextureStorage2D, so the size, format
	 * and level count can never change and the texture is always complete. To
	 * change an image, build a new Texture2D and move it over the old one.
	 *
	 * Row 0 of the source bytes is the TOP of the image and is uploaded
	 * as-is (see vox::Resource), so UV (0, 0) is the top-left corner.
	 *
	 * Construction throws std::runtime_error when the image cannot be uploaded
	 * - unsupported format, too large for the driver - so a Texture2D that
	 * exists is always valid unless it has been moved from. GL thread only.
	 *
	 */
	class Texture2D
	{
	public:

		/*!
			@brief
				Uploads a decoded image, every level it carries.

				Uncompressed images with one level get a full mip chain generated
				on the GPU. Block-compressed images keep exactly the levels the
				file stores, because the driver cannot regenerate them.
			@param image
				A ResourceKind::Texture resource
			@param colorSpace
				How to interpret the colour. The request wins over the file's own
				sRGB flag; formats with no sRGB variant (BC4, BC5, BC6H, RGBA16F)
				are always Linear.
			@param label
				Name shown by RenderDoc / Nsight and in GL debug messages
			*/
		Texture2D(const vox::Resource& image, ColorSpace colorSpace, std::string_view label);

		/*!
			@brief
				Uploads raw uncompressed pixels, for built-in and generated textures.
			@param width
				Image width in texels
			@param height
				Image height in texels
			@param format
				RGBA8 or RGBA16F
			@param pixels
				width * height texels, top row first
			@param colorSpace
				How to interpret the colour
			@param label
				Name shown by RenderDoc / Nsight and in GL debug messages
			@param generateMips
				Allocate and fill a full mip chain
			*/
		Texture2D(int width, int height, vox::PixelFormat format, std::span<const unsigned char> pixels,
			ColorSpace colorSpace, std::string_view label, bool generateMips = true);

		/*!
			@brief
				Deletes the GL texture if still owned.
			*/
		~Texture2D();

		// NO COPY
		Texture2D(const Texture2D&) = delete;
		Texture2D& operator=(const Texture2D&) = delete;

		// YES MOVE
		/*!
			@brief
				Takes over another texture's GL object, leaving it invalid.
			@param other
				Texture to move from
			*/
		Texture2D(Texture2D&& other) noexcept;

		/*!
			@brief
				Releases this texture and takes over another's GL object.
			@param other
				Texture to move from
			@return
				This texture
			*/
		Texture2D& operator=(Texture2D&& other) noexcept;

		/*!
			@brief
				Binds to a texture unit. Pair with a sampler on the same unit.
			@param unit
				Texture unit to bind to
			*/
		void bind(TextureUnit unit) const;

		/*!
			@brief
				Gets the GL texture name.
			@return
				The GL texture id, 0 when moved from
			*/
		GLuint		id() const			{ return id_; }

		/*!
			@brief
				Gets the base level width.
			@return
				Width in texels
			*/
		int			width() const		{ return width_; }

		/*!
			@brief
				Gets the base level height.
			@return
				Height in texels
			*/
		int			height() const		{ return height_; }

		/*!
			@brief
				Gets the number of mip levels in storage.
			@return
				Mip level count
			*/
		int			levels() const		{ return levels_; }

		/*!
			@brief
				Gets the GL internal format the texture is stored as.
			@return
				The GL internal format enum
			*/
		GLenum		internalFormat() const { return internalFormat_; }

		/*!
			@brief
				Gets the colour space the texture was actually stored in.
			@return
				Srgb or Linear
			*/
		ColorSpace	colorSpace() const	{ return colorSpace_; }

		/*!
			@brief
				Whether this object owns a GL texture.
			@return
				False only after being moved from
			*/
		bool		isValid() const		{ return id_ != 0; }

		/*!
			@brief
				Picks the GL internal format for a pixel format and colour space.
			@param format
				Pixel format of the image
			@param colorSpace
				Requested colour space
			@return
				The GL internal format this texture would be stored as, or 0 if the
				running driver cannot store it
			*/
		static GLenum internalFormatFor(vox::PixelFormat format, ColorSpace colorSpace);

		/*!
			@brief
				Prints which block-compressed families the running driver can store.

				Covers S3TC (BC1/BC3), RGTC (BC4/BC5) and BPTC (BC6H/BC7). The
				hardware decides support, so this is logged once at graphics init.
			*/
		static void logCompressionSupport();

	private:

		struct Image
		{
			int									width_	{ 0 };
			int									height_	{ 0 };
			vox::PixelFormat					format_	{ vox::PixelFormat::Unknown };
			std::span<const vox::MipLevel>		mips_;
			std::span<const unsigned char>		bytes_;
		};

		/*!
			@brief
				Validates an image, allocates immutable storage and uploads every level; throws on failure.
			@param image
				Size, format, mip table and bytes to upload
			@param colorSpace
				Requested colour space
			@param label
				Debug name, also used in error messages
			@param generateMips
				Generate a full chain on the GPU for a single-level uncompressed image
			*/
		void create(const Image& image, ColorSpace colorSpace, std::string_view label, bool generateMips);

		/*!
			@brief
				Deletes the GL texture if one is owned and resets the id to 0.
			*/
		void release() noexcept;

		GLuint		id_				{ 0 };
		int			width_			{ 0 };
		int			height_			{ 0 };
		int			levels_			{ 0 };
		GLenum		internalFormat_	{ 0 };
		ColorSpace	colorSpace_		{ ColorSpace::Linear };
	};
}
