/*************************************************************************************************************
\file       texture.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Texture2D: immutable DSA texture storage, validated upload of compressed and uncompressed
            mip chains, GL format selection per pixel format and colour space, driver support logging.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "texture.hpp"

// stds
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>


namespace graphics::texture
{
	namespace
	{
		/**
		 * Puts every unpack parameter an upload depends on into a known state
		 * and restores the caller's on scope exit.
		 *
		 * ImGui's OpenGL backend and any future streaming code change these, and
		 * a pixel-unpack buffer left bound turns the data pointer into an offset
		 * into that buffer - an upload of garbage with no error.
		 *
		 */
		class UnpackStateGuard
		{
		public:
			/*!
				@brief
					Saves the current unpack state, then sets tight packing and unbinds any pixel-unpack buffer.
				*/
			UnpackStateGuard()
			{
				glGetIntegerv(GL_UNPACK_ALIGNMENT,			&alignment_);
				glGetIntegerv(GL_UNPACK_ROW_LENGTH,			&rowLength_);
				glGetIntegerv(GL_UNPACK_SKIP_ROWS,			&skipRows_);
				glGetIntegerv(GL_UNPACK_SKIP_PIXELS,		&skipPixels_);
				glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpackBuffer_);

				glPixelStorei(GL_UNPACK_ALIGNMENT,		1);
				glPixelStorei(GL_UNPACK_ROW_LENGTH,		0);
				glPixelStorei(GL_UNPACK_SKIP_ROWS,		0);
				glPixelStorei(GL_UNPACK_SKIP_PIXELS,	0);
				glBindBuffer(GL_PIXEL_UNPACK_BUFFER,	0);
			}

			/*!
				@brief
					Restores the unpack state saved by the constructor.
				*/
			~UnpackStateGuard()
			{
				glPixelStorei(GL_UNPACK_ALIGNMENT,		alignment_);
				glPixelStorei(GL_UNPACK_ROW_LENGTH,		rowLength_);
				glPixelStorei(GL_UNPACK_SKIP_ROWS,		skipRows_);
				glPixelStorei(GL_UNPACK_SKIP_PIXELS,	skipPixels_);
				glBindBuffer(GL_PIXEL_UNPACK_BUFFER,	static_cast<GLuint>(unpackBuffer_));
			}

			UnpackStateGuard(const UnpackStateGuard&) = delete;
			UnpackStateGuard& operator=(const UnpackStateGuard&) = delete;

		private:
			GLint	alignment_		{ 4 };
			GLint	rowLength_		{ 0 };
			GLint	skipRows_		{ 0 };
			GLint	skipPixels_		{ 0 };
			GLint	unpackBuffer_	{ 0 };
		};

		/*!
			@brief
				Counts the levels of a full mip chain down to 1x1.
			@param width
				Base level width in texels
			@param height
				Base level height in texels
			@return
				Number of mip levels, including the base
			*/
		int fullMipCount(int width, int height)
		{
			int levels = 1;

			for (int edge = std::max(width, height); edge > 1; edge >>= 1)
				++levels;

			return levels;
		}

		/*!
			@brief
				Whether the driver supports S3TC (BC1/BC3).

				S3TC is an extension even in 4.6 - universal on desktop, but checked.
			@return
				True when S3TC is available
			*/
		bool hasS3tc()		{ return GLEW_EXT_texture_compression_s3tc != 0; }

		/*!
			@brief
				Whether the driver supports sRGB S3TC.

				The sRGB variants arrive through either of two extensions.
			@return
				True when sRGB S3TC is available
			*/
		bool hasS3tcSrgb()	{ return hasS3tc() && (GLEW_EXT_texture_sRGB || GLEW_EXT_texture_compression_s3tc_srgb); }

		/*!
			@brief
				The colour space a format can actually be stored in.

				BC4, BC5, BC6H and floating point have no sRGB variant: they hold
				data or HDR light.
			@param format
				Pixel format of the image
			@param requested
				Colour space the caller asked for
			@return
				The requested space, or Linear when the format has no sRGB variant
			*/
		ColorSpace effectiveColorSpace(vox::PixelFormat format, ColorSpace requested)
		{
			switch (format)
			{
			case vox::PixelFormat::RGBA8:
			case vox::PixelFormat::BC1:
			case vox::PixelFormat::BC3:
			case vox::PixelFormat::BC7:
				return requested;

			case vox::PixelFormat::BC4:
			case vox::PixelFormat::BC5:
			case vox::PixelFormat::BC6H_UF16:
			case vox::PixelFormat::BC6H_SF16:
			case vox::PixelFormat::RGBA16F:
			case vox::PixelFormat::Unknown:
			default:
				return ColorSpace::Linear;
			}
		}

		/*!
			@brief
				Attaches a debug label to a texture, truncated to the driver's limit; no-op without KHR_debug.
			@param id
				GL texture name
			@param label
				Label text; empty skips labelling
			*/
		void applyLabel(GLuint id, std::string_view label)
		{
			if (label.empty() || !(GLEW_VERSION_4_3 || GLEW_KHR_debug))
				return;

			GLint maxLength = 0;
			glGetIntegerv(GL_MAX_LABEL_LENGTH, &maxLength);

			// The limit counts the terminator.
			const std::size_t length = std::min<std::size_t>(label.size(), static_cast<std::size_t>(std::max(maxLength - 1, 0)));

			glObjectLabel(GL_TEXTURE, id, static_cast<GLsizei>(length), label.data());
		}
	}

	/*!
		@brief
			Picks the GL internal format for a pixel format and colour space.
		@param format
			Pixel format of the image
		@param colorSpace
			Requested colour space; ignored for formats without an sRGB variant
		@return
			The GL internal format, or 0 if the running driver cannot store it
		*/
	GLenum Texture2D::internalFormatFor(vox::PixelFormat format, ColorSpace colorSpace)
	{
		const bool srgb = effectiveColorSpace(format, colorSpace) == ColorSpace::Srgb;

		switch (format)
		{
		case vox::PixelFormat::RGBA8:
			return srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;

		case vox::PixelFormat::RGBA16F:
			return GL_RGBA16F;

		// The RGBA variant of BC1, never the RGB one: the RGB formats decode
		// punch-through blocks' transparent index as opaque black.
		case vox::PixelFormat::BC1:
			if (srgb)
				return hasS3tcSrgb() ? GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT : 0;
			return hasS3tc() ? GL_COMPRESSED_RGBA_S3TC_DXT1_EXT : 0;

		case vox::PixelFormat::BC3:
			if (srgb)
				return hasS3tcSrgb() ? GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT : 0;
			return hasS3tc() ? GL_COMPRESSED_RGBA_S3TC_DXT5_EXT : 0;

		// RGTC is core since 3.0, BPTC since 4.2.
		case vox::PixelFormat::BC4:
			return GL_COMPRESSED_RED_RGTC1;

		case vox::PixelFormat::BC5:
			return GL_COMPRESSED_RG_RGTC2;

		case vox::PixelFormat::BC6H_UF16:
			return GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT;

		case vox::PixelFormat::BC6H_SF16:
			return GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT;

		case vox::PixelFormat::BC7:
			return srgb ? GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM : GL_COMPRESSED_RGBA_BPTC_UNORM;

		case vox::PixelFormat::Unknown:
		default:
			return 0;
		}
	}

	/*!
		@brief
			Prints which block-compressed families (S3TC, RGTC, BPTC) the running driver can store.
		*/
	void Texture2D::logCompressionSupport()
	{
		const auto yesNo = [](vox::PixelFormat format, ColorSpace colorSpace)
		{
			return internalFormatFor(format, colorSpace) != 0 ? "yes" : "NO";
		};

		// What the driver can store decides which cooked formats load; anything
		// unsupported resolves to the missing texture instead.
		const auto* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));

		std::cout
			<< "[texture] compression support on " << (renderer ? renderer : "unknown renderer") << "\n"
			<< "[texture]   S3TC BC1/BC3 (extension):  " << yesNo(vox::PixelFormat::BC1, ColorSpace::Linear)
			<< ", sRGB " << yesNo(vox::PixelFormat::BC1, ColorSpace::Srgb) << "\n"
			<< "[texture]   RGTC BC4/BC5 (core 3.0):   " << yesNo(vox::PixelFormat::BC5, ColorSpace::Linear) << "\n"
			<< "[texture]   BPTC BC6H/BC7 (core 4.2):  " << yesNo(vox::PixelFormat::BC6H_UF16, ColorSpace::Linear)
			<< ", BC7 sRGB " << yesNo(vox::PixelFormat::BC7, ColorSpace::Srgb) << "\n";
	}

	/*!
		@brief
			Uploads a texture resource with every level it carries; throws if it is not a texture.
		@param image
			A ResourceKind::Texture resource
		@param colorSpace
			How to interpret the colour; warns when an sRGB-authored image is requested as linear
		@param label
			Debug name for the GL object
		*/
	Texture2D::Texture2D(const vox::Resource& image, ColorSpace colorSpace, std::string_view label)
	{
		if (image.kind != vox::ResourceKind::Texture)
			throw std::runtime_error("resource is not a texture");

		if (image.srgbHint && colorSpace == ColorSpace::Linear)
			std::cerr << "[Texture2D] " << label << " is authored as sRGB but was requested as linear data\n";

		create(Image{ image.width, image.height, image.format, image.mips, image.bytes },
			colorSpace, label, image.mips.size() == 1);
	}

	/*!
		@brief
			Uploads raw uncompressed pixels as a single base level; throws for block-compressed formats.
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
			Debug name for the GL object
		@param generateMips
			Allocate and fill a full mip chain
		*/
	Texture2D::Texture2D(int width, int height, vox::PixelFormat format, std::span<const unsigned char> pixels,
		ColorSpace colorSpace, std::string_view label, bool generateMips)
	{
		if (vox::isBlockCompressed(format))
			throw std::runtime_error("raw pixel upload takes uncompressed formats only");

		const vox::MipLevel base{ 0, pixels.size(), width, height };

		create(Image{ width, height, format, std::span<const vox::MipLevel>(&base, 1), pixels },
			colorSpace, label, generateMips);
	}

	/*!
		@brief
			Validates an image against its bytes, allocates immutable storage and uploads every level.

			Every mip level is checked before any GL call so a bad resource cannot
			make the driver read past the buffer. Throws std::runtime_error on any
			failure; on success replaces whatever texture this object held.
		@param image
			Size, format, mip table and bytes to upload
		@param colorSpace
			Requested colour space
		@param label
			Debug name, also used in error messages
		@param generateMips
			Generate a full chain on the GPU for a single-level uncompressed image
		*/
	void Texture2D::create(const Image& image, ColorSpace colorSpace, std::string_view label, bool generateMips)
	{
		const std::string name{ label };

		if (image.width_ <= 0 || image.height_ <= 0 || image.mips_.empty())
			throw std::runtime_error(name + ": empty image");

		GLint maxSize = 0;
		glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);

		if (image.width_ > maxSize || image.height_ > maxSize)
			throw std::runtime_error(name + ": " + std::to_string(image.width_) + "x" + std::to_string(image.height_)
				+ " exceeds GL_MAX_TEXTURE_SIZE " + std::to_string(maxSize));

		const bool compressed = vox::isBlockCompressed(image.format_);
		const GLenum internalFormat = internalFormatFor(image.format_, colorSpace);

		if (internalFormat == 0)
			throw std::runtime_error(name + ": the driver cannot store " + vox::pixelFormatName(image.format_)
				+ (colorSpace == ColorSpace::Srgb ? " as sRGB" : ""));

		const ColorSpace stored = effectiveColorSpace(image.format_, colorSpace);

		if (stored != colorSpace)
			std::cerr << "[Texture2D] " << name << ": " << vox::pixelFormatName(image.format_)
				<< " has no sRGB variant, stored as linear\n";

		// Uploaded pixel type for uncompressed data. RGBA16F comes as half floats.
		const GLenum pixelType = image.format_ == vox::PixelFormat::RGBA16F ? GL_HALF_FLOAT : GL_UNSIGNED_BYTE;
		const std::size_t bytesPerTexel = image.format_ == vox::PixelFormat::RGBA16F ? 8 : 4;

		const int maxLevels = fullMipCount(image.width_, image.height_);
		const int storedLevels = static_cast<int>(image.mips_.size());

		if (storedLevels > maxLevels)
			throw std::runtime_error(name + ": more mip levels than the size allows");

		// Validate every level against the bytes before touching GL, so a bad
		// resource can never make the driver read past the buffer.
		for (int i = 0; i < storedLevels; ++i)
		{
			const vox::MipLevel& mip = image.mips_[static_cast<std::size_t>(i)];

			const bool sizeMatches =
				mip.width == std::max(1, image.width_ >> i) &&
				mip.height == std::max(1, image.height_ >> i);

			const bool inBounds = mip.offset <= image.bytes_.size() && mip.size <= image.bytes_.size() - mip.offset;

			const bool texelsMatch = compressed ||
				mip.size == static_cast<std::size_t>(mip.width) * static_cast<std::size_t>(mip.height) * bytesPerTexel;

			if (!sizeMatches || !inBounds || !texelsMatch)
				throw std::runtime_error(name + ": mip level " + std::to_string(i) + " does not match its data");
		}

		// Compressed textures keep the file's chain. Uncompressed ones either
		// keep theirs or get a full generated chain.
		const bool generate = !compressed && generateMips && storedLevels == 1;
		const int levels = generate ? maxLevels : storedLevels;

		GLuint id = 0;
		glCreateTextures(GL_TEXTURE_2D, 1, &id);

		if (id == 0)
			throw std::runtime_error(name + ": glCreateTextures failed");

		glTextureStorage2D(id, levels, internalFormat, image.width_, image.height_);

		{
			UnpackStateGuard unpack;

			for (int i = 0; i < storedLevels; ++i)
			{
				const vox::MipLevel& mip = image.mips_[static_cast<std::size_t>(i)];
				const unsigned char* data = image.bytes_.data() + mip.offset;

				if (compressed)
					glCompressedTextureSubImage2D(id, i, 0, 0, mip.width, mip.height,
						internalFormat, static_cast<GLsizei>(mip.size), data);
				else
					glTextureSubImage2D(id, i, 0, 0, mip.width, mip.height, GL_RGBA, pixelType, data);
			}
		}

		// Legal on SRGB8_ALPHA8: it is filterable and colour-renderable, and
		// current drivers average in linear space. Never on compressed storage.
		if (generate && levels > 1)
			glGenerateTextureMipmap(id);

		// The texture's own sampling state is never used - every draw binds a
		// sampler object - but keep it consistent with the level count so a
		// bind without a sampler still samples a complete texture.
		glTextureParameteri(id, GL_TEXTURE_BASE_LEVEL, 0);
		glTextureParameteri(id, GL_TEXTURE_MAX_LEVEL, levels - 1);

		// BC4 stores one channel (masks, distance fields). Unswizzled it samples
		// as (r, 0, 0, 1) and previews red; mirrored into RGB it reads as grey,
		// and shaders that read .r are unaffected.
		if (image.format_ == vox::PixelFormat::BC4)
		{
			const GLint swizzle[4]{ GL_RED, GL_RED, GL_RED, GL_ONE };
			glTextureParameteriv(id, GL_TEXTURE_SWIZZLE_RGBA, swizzle);
		}

		applyLabel(id, label);

		release();

		id_				= id;
		width_			= image.width_;
		height_			= image.height_;
		levels_			= levels;
		internalFormat_	= internalFormat;
		colorSpace_		= stored;
	}

	/*!
		@brief
			Deletes the GL texture if still owned.
		*/
	Texture2D::~Texture2D()
	{
		release();
	}

	/*!
		@brief
			Takes over another texture's GL object, leaving it invalid.
		@param other
			Texture to move from
		*/
	Texture2D::Texture2D(Texture2D&& other) noexcept
		:
		id_				{ other.id_ },
		width_			{ other.width_ },
		height_			{ other.height_ },
		levels_			{ other.levels_ },
		internalFormat_	{ other.internalFormat_ },
		colorSpace_		{ other.colorSpace_ }
	{
		other.id_ = 0;
	}

	/*!
		@brief
			Releases this texture and takes over another's GL object.
		@param other
			Texture to move from
		@return
			This texture
		*/
	Texture2D& Texture2D::operator=(Texture2D&& other) noexcept
	{
		if (this != &other)
		{
			release();

			id_				= other.id_;
			width_			= other.width_;
			height_			= other.height_;
			levels_			= other.levels_;
			internalFormat_	= other.internalFormat_;
			colorSpace_		= other.colorSpace_;

			other.id_ = 0;
		}

		return *this;
	}

	/*!
		@brief
			Binds to a texture unit. Pair with a sampler on the same unit.
		@param unit
			Texture unit to bind to
		*/
	void Texture2D::bind(TextureUnit unit) const
	{
		glBindTextureUnit(static_cast<GLuint>(unit), id_);
	}

	/*!
		@brief
			Deletes the GL texture if one is owned and resets the id to 0.
		*/
	void Texture2D::release() noexcept
	{
		if (id_ != 0)
		{
			glDeleteTextures(1, &id_);
			id_ = 0;
		}
	}
}
