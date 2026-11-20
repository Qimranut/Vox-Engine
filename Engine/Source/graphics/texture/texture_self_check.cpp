/*************************************************************************************************************
\file       texture_self_check.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Startup texture self-check: row order, sRGB storage, mips, the DDS parser with hand-built
            BC1/BC4/BC5/BC6H/BC7 blocks, cooked paths, the .vmdl reader, TextureManager and samplers.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "texture_self_check.hpp"

// Texture
#include "normal_decode.hpp"
#include "sampler.hpp"
#include "texture.hpp"
#include "texture_manager.hpp"

// Resources
#include "resources/ResourceManager.hpp"

// GLEW
#include <GL/glew.h>

// stds
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <span>
#include <string>
#include <vector>


namespace graphics::texture
{
	namespace
	{
		/*!
			@brief
				Reads one level back as RGBA8 with pack state pinned, whatever the
				stored format.

				The driver decodes compressed data; sRGB storage is returned as
				stored, not converted.
			@param texture
				Texture to read
			@param level
				Mip level to read
			@return
				The level's texels as RGBA8, top row first
			*/
		std::vector<unsigned char> readLevel(const Texture2D& texture, int level)
		{
			const int w = std::max(1, texture.width() >> level);
			const int h = std::max(1, texture.height() >> level);

			std::vector<unsigned char> pixels(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);

			GLint alignment = 4;
			GLint packBuffer = 0;
			glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
			glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &packBuffer);

			glPixelStorei(GL_PACK_ALIGNMENT, 1);
			glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

			glGetTextureImage(texture.id(), level, GL_RGBA, GL_UNSIGNED_BYTE,
				static_cast<GLsizei>(pixels.size()), pixels.data());

			glPixelStorei(GL_PACK_ALIGNMENT, alignment);
			glBindBuffer(GL_PIXEL_PACK_BUFFER, static_cast<GLuint>(packBuffer));

			return pixels;
		}

		/*!
			@brief
				Writes a 32-bit value into a byte buffer at an offset (native byte order).
			@param bytes
				Buffer to write into; must already be large enough
			@param offset
				Byte offset of the value
			@param value
				Value to write
			*/
		void putU32(std::vector<unsigned char>& bytes, std::size_t offset, std::uint32_t value)
		{
			std::memcpy(bytes.data() + offset, &value, sizeof(value));
		}

		/*!
			@brief
				Builds a 4x4 DXT1 DDS in memory.

				Endpoints are exact 5:6:5 red and blue with color0 > color1
				(four-colour mode), and only indices 0 and 1 are used, so decoding
				involves no interpolation and no driver rounding. The top two rows
				are red, the bottom two blue.
			@param mipCount
				Value written to the header's mip count
			@param withMipFlag
				Whether to set the DDSD_MIPMAPCOUNT flag
			@return
				The DDS file bytes
			*/
		std::vector<unsigned char> makeBc1Dds(std::uint32_t mipCount, bool withMipFlag)
		{
			std::vector<unsigned char> dds(4 + 124 + 8, 0);

			const std::uint32_t flags = 0x1 | 0x2 | 0x4 | 0x1000 | (withMipFlag ? 0x20000u : 0u);

			putU32(dds, 0,			0x20534444);				// "DDS "
			putU32(dds, 4,			124);						// dwSize
			putU32(dds, 4 + 4,		flags);
			putU32(dds, 4 + 8,		4);							// height
			putU32(dds, 4 + 12,		4);							// width
			putU32(dds, 4 + 24,		mipCount);
			putU32(dds, 4 + 72,		32);						// ddspf.dwSize
			putU32(dds, 4 + 76,		0x4);						// DDPF_FOURCC
			putU32(dds, 4 + 80,		0x31545844);				// "DXT1"
			putU32(dds, 4 + 104,	0x1000);					// DDSCAPS_TEXTURE

			std::uint32_t indices = 0;

			for (std::uint32_t texel = 8; texel < 16; ++texel)
				indices |= 1u << (texel * 2);					// rows 2-3 -> index 1

			const std::size_t block = 4 + 124;
			const std::uint16_t red = 0xF800, blue = 0x001F;

			std::memcpy(dds.data() + block,		&red,		sizeof(red));
			std::memcpy(dds.data() + block + 2,	&blue,		sizeof(blue));
			std::memcpy(dds.data() + block + 4,	&indices,	sizeof(indices));

			return dds;
		}

		/*!
			@brief
				Builds a 4x4, single-level DDS with a DX10 header, the layout
				TextureCompiler writes for every format.
			@param dxgi
				DXGI_FORMAT value
			@param block
				The one block of image data (8 or 16 bytes)
			@return
				The DDS file bytes
			*/
		std::vector<unsigned char> makeDx10Dds(std::uint32_t dxgi, std::span<const unsigned char> block)
		{
			std::vector<unsigned char> dds(4 + 124 + 20, 0);

			putU32(dds, 0,			0x20534444);				// "DDS "
			putU32(dds, 4,			124);						// dwSize
			putU32(dds, 4 + 4,		0x1 | 0x2 | 0x4 | 0x1000);
			putU32(dds, 4 + 8,		4);							// height
			putU32(dds, 4 + 12,		4);							// width
			putU32(dds, 4 + 72,		32);						// ddspf.dwSize
			putU32(dds, 4 + 76,		0x4);						// DDPF_FOURCC
			putU32(dds, 4 + 80,		0x30315844);				// "DX10"
			putU32(dds, 4 + 104,	0x1000);					// DDSCAPS_TEXTURE
			putU32(dds, 128,		dxgi);
			putU32(dds, 128 + 4,	3);							// DDS_DIMENSION_TEXTURE2D
			putU32(dds, 128 + 12,	1);							// arraySize

			dds.insert(dds.end(), block.begin(), block.end());

			return dds;
		}

		/*!
			@brief
				Queries the internal format the driver actually stored level 0 in.
			@param texture
				Texture to query
			@return
				The GL internal format of level 0
			*/
		GLint levelInternalFormat(const Texture2D& texture)
		{
			GLint stored = 0;
			glGetTextureLevelParameteriv(texture.id(), 0, GL_TEXTURE_INTERNAL_FORMAT, &stored);
			return stored;
		}

		/*!
			@brief
				Checks whether the DDS parser rejects a file.
			@param file
				DDS file bytes
			@return
				True if vox::parseDds threw
			*/
		bool throws(const std::vector<unsigned char>& file)
		{
			vox::Resource image;

			try
			{
				vox::parseDds(file, image);
			}
			catch (const std::exception&)
			{
				return true;
			}

			return false;
		}
	}

	/*!
		@brief
			Verifies the texture path against the real driver at startup.

			Uses in-memory images and a throwaway TextureManager; reports to
			stdout/stderr and never throws. Needs a current OpenGL 4.5 context.
		@param resources
			Used for cooked-path checks and to prove a missing file falls back cleanly
		@return
			True if every check passed
		*/
	bool runSelfCheck(vox::ResourceManager& resources)
	{
		int failures = 0;

		const auto check = [&failures](bool ok, const char* what)
		{
			if (!ok)
			{
				std::cerr << "[texture-check] FAILED: " << what << "\n";
				++failures;
			}
		};

		// Start from a clean error flag so a failure below is ours.
		for (GLenum error = glGetError(); error != GL_NO_ERROR; error = glGetError())
			std::cerr << "[texture-check] note: GL error 0x" << std::hex << error << std::dec
				<< " was pending from earlier code\n";

		try
		{
			// ------------------- Row order ----------------------
			//
			// Row 0 of the source is the top of the image and must come back
			// first. An upload that flips, or reads with the wrong alignment,
			// fails here instead of as an upside-down texture in a scene.
			{
				const std::array<unsigned char, 16> pixels
				{
					255, 0, 0, 255,			0, 255, 0, 255,			// top:    red, green
					0, 0, 255, 255,			255, 255, 255, 255		// bottom: blue, white
				};

				const Texture2D texture{ 2, 2, vox::PixelFormat::RGBA8, pixels, ColorSpace::Linear,
					"texture-check/rows", false };

				check(texture.levels() == 1, "generateMips=false still allocated a chain");
				check(texture.internalFormat() == GL_RGBA8, "linear RGBA8 is not stored as GL_RGBA8");

				const std::vector<unsigned char> back = readLevel(texture, 0);
				check(std::memcmp(back.data(), pixels.data(), pixels.size()) == 0,
					"RGBA8 readback differs - rows flipped or channels swapped");
			}

			// ------------------- sRGB storage -------------------
			//
			// sRGB storage decodes when SAMPLED, not when read back, so the
			// stored byte survives unchanged. A format that silently fell back
			// to RGBA8 would light every albedo in gamma space.
			{
				const std::array<unsigned char, 4> grey{ 128, 128, 128, 255 };

				const Texture2D texture{ 1, 1, vox::PixelFormat::RGBA8, grey, ColorSpace::Srgb,
					"texture-check/srgb", false };

				GLint stored = 0;
				glGetTextureLevelParameteriv(texture.id(), 0, GL_TEXTURE_INTERNAL_FORMAT, &stored);

				check(stored == GL_SRGB8_ALPHA8, "sRGB colour is not stored as GL_SRGB8_ALPHA8");
				check(texture.colorSpace() == ColorSpace::Srgb, "sRGB request reported as linear");
				check(readLevel(texture, 0)[0] == 128, "sRGB texel was converted on readback");
			}

			// ------------------- Mip chain ----------------------
			{
				const std::vector<unsigned char> pixels(5 * 3 * 4, 200);

				const Texture2D texture{ 5, 3, vox::PixelFormat::RGBA8, pixels, ColorSpace::Srgb,
					"texture-check/npot" };

				GLint immutableLevels = 0;
				glGetTextureParameteriv(texture.id(), GL_TEXTURE_IMMUTABLE_LEVELS, &immutableLevels);

				check(texture.levels() == 3, "5x3 image should get 3 mip levels (5x3, 2x1, 1x1)");
				check(immutableLevels == 3, "immutable storage level count disagrees");
			}

			// sRGB-correct mip averaging. The spec leaves the filter open, so
			// a driver averaging raw bytes is a warning, not a failure: black
			// and white should average to 188 (linear 0.5), not 128.
			{
				const std::array<unsigned char, 8> pixels{ 0, 0, 0, 255,	255, 255, 255, 255 };

				const Texture2D texture{ 2, 1, vox::PixelFormat::RGBA8, pixels, ColorSpace::Srgb,
					"texture-check/srgb-mips" };

				const int mid = readLevel(texture, 1)[0];

				if (mid < 170 || mid > 205)
					std::cerr << "[texture-check] warning: this driver averages sRGB mips in gamma space"
						<< " (black+white -> " << mid << ", expected ~188); distant textures will look dark\n";
			}

			// ------------------- DDS parser ---------------------
			{
				const std::vector<unsigned char> file = makeBc1Dds(0, false);

				vox::Resource image;
				image.kind = vox::ResourceKind::Texture;
				vox::parseDds(file, image);

				check(image.format == vox::PixelFormat::BC1, "DXT1 FourCC not parsed as BC1");
				check(image.width == 4 && image.height == 4, "DDS dimensions misread");
				check(image.mips.size() == 1 && image.mips[0].size == 8, "mip count 0 must mean one 8-byte level");
				check(!image.srgbHint, "legacy FourCC DDS claimed to be sRGB");

				std::vector<unsigned char> truncated = file;
				truncated.resize(truncated.size() - 4);
				check(throws(truncated), "truncated DDS was accepted");

				check(throws(makeBc1Dds(4, true)), "4x4 DDS claiming 4 mip levels was accepted");

				std::vector<unsigned char> premultiplied = file;
				putU32(premultiplied, 4 + 80, 0x32545844);		// "DXT2"
				check(throws(premultiplied), "premultiplied DXT2 was accepted");

				std::vector<unsigned char> cubemap = file;
				putU32(cubemap, 4 + 108, 0x200);				// DDSCAPS2_CUBEMAP
				check(throws(cubemap), "cubemap DDS was accepted as 2D");

				// ------------- BC1 upload and decode ----------------
				if (Texture2D::internalFormatFor(vox::PixelFormat::BC1, ColorSpace::Linear) == 0)
				{
					std::cerr << "[texture-check] note: no S3TC support, BC1 decode not checked\n";
				}
				else
				{
					const Texture2D texture{ image, ColorSpace::Linear, "texture-check/bc1" };
					const std::vector<unsigned char> back = readLevel(texture, 0);

					check(texture.levels() == 1, "compressed texture gained generated mips");
					check(back[0] == 255 && back[1] == 0 && back[2] == 0 && back[3] == 255,
						"BC1 top-left texel is not red - block data uploaded wrong");
					check(back[12 * 4] == 0 && back[12 * 4 + 1] == 0 && back[12 * 4 + 2] == 255,
						"BC1 bottom-left texel is not blue - block rows reversed");
				}
			}

			// ------------------- BC4: one channel ---------------
			//
			// red0 > red1 selects the 8-value palette and every 3-bit index is
			// 0, so each texel is exactly red0 - no interpolation to round.
			{
				const std::array<unsigned char, 8> block{ 200, 100, 0, 0, 0, 0, 0, 0 };

				vox::Resource image;
				image.kind = vox::ResourceKind::Texture;
				vox::parseDds(makeDx10Dds(80, block), image);			// DXGI_FORMAT_BC4_UNORM

				check(image.format == vox::PixelFormat::BC4 && image.mips[0].size == 8, "DXGI BC4_UNORM misparsed");

				const Texture2D texture{ image, ColorSpace::Linear, "texture-check/bc4" };
				const std::vector<unsigned char> back = readLevel(texture, 0);

				check(back[0] == 200, "BC4 texel does not decode to its endpoint");

				// Readback ignores swizzle; sampling applies it.
				GLint swizzle[4]{};
				glGetTextureParameteriv(texture.id(), GL_TEXTURE_SWIZZLE_RGBA, swizzle);
				check(swizzle[0] == GL_RED && swizzle[1] == GL_RED && swizzle[2] == GL_RED && swizzle[3] == GL_ONE,
					"BC4 is not swizzled to grey (R,R,R,1)");
			}

			// ------------------- BC5: normal X,Y ----------------
			//
			// Two BC4 blocks, red then green. Z is not stored; decodeNormalBC5
			// rebuilds it from X^2 + Y^2 + Z^2 = 1.
			{
				const std::array<unsigned char, 16> block
				{
					230, 100, 0, 0, 0, 0, 0, 0,		// red   (X) = 230
					128, 50, 0, 0, 0, 0, 0, 0		// green (Y) = 128
				};

				vox::Resource image;
				image.kind = vox::ResourceKind::Texture;
				vox::parseDds(makeDx10Dds(83, block), image);			// DXGI_FORMAT_BC5_UNORM

				check(image.format == vox::PixelFormat::BC5 && image.sourceChannels == 2, "DXGI BC5_UNORM misparsed");

				const Texture2D texture{ image, ColorSpace::Linear, "texture-check/bc5" };
				const std::vector<unsigned char> back = readLevel(texture, 0);

				check(back[0] == 230 && back[1] == 128, "BC5 red/green do not decode to their endpoints");
				check(levelInternalFormat(texture) == GL_COMPRESSED_RG_RGTC2, "BC5 is not stored as RGTC2");

				const float r = back[0] / 255.f;
				const float g = back[1] / 255.f;
				const glm::vec3 n = decodeNormalBC5(r, g);

				const float x = r * 2.f - 1.f;
				const float y = g * 2.f - 1.f;

				check(std::abs(glm::length(n) - 1.f) < 1e-4f, "BC5 decoded normal is not unit length");
				check(std::abs(n.z - std::sqrt(1.f - x * x - y * y)) < 1e-3f, "BC5 Z is not sqrt(1 - X^2 - Y^2)");
				check(decodeNormalBC5(0.5f, 0.5f).z > 0.999f, "flat BC5 texel does not point out of the surface");
			}

			// ------------------- BC6H: HDR ----------------------
			//
			// An all-zero block is mode 0 with zero endpoints: black. Encoding a
			// bright block by hand is fragile; this proves the parse, the float
			// BPTC storage and a GL-error-free upload.
			{
				const std::array<unsigned char, 16> block{};

				vox::Resource image;
				image.kind = vox::ResourceKind::Texture;
				vox::parseDds(makeDx10Dds(95, block), image);			// DXGI_FORMAT_BC6H_UF16

				check(image.format == vox::PixelFormat::BC6H_UF16 && image.mips[0].size == 16,
					"DXGI BC6H_UF16 misparsed");

				vox::Resource signedImage;
				signedImage.kind = vox::ResourceKind::Texture;
				vox::parseDds(makeDx10Dds(96, block), signedImage);		// DXGI_FORMAT_BC6H_SF16
				check(signedImage.format == vox::PixelFormat::BC6H_SF16, "DXGI BC6H_SF16 misparsed");

				check(Texture2D::internalFormatFor(vox::PixelFormat::BC6H_UF16, ColorSpace::Srgb)
					== GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT, "BC6H did not ignore an sRGB request");

				const Texture2D texture{ image, ColorSpace::Linear, "texture-check/bc6h" };

				check(levelInternalFormat(texture) == GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT,
					"BC6H is not stored as unsigned-float BPTC");

				std::array<float, 16 * 4> texels{};
				glGetTextureImage(texture.id(), 0, GL_RGBA, GL_FLOAT, static_cast<GLsizei>(sizeof(texels)), texels.data());

				check(texels[0] == 0.f && texels[1] == 0.f && texels[2] == 0.f && texels[3] == 1.f,
					"zero BC6H block does not decode to opaque black");
			}

			// ------------------- BC7 sRGB -----------------------
			{
				const std::array<unsigned char, 16> block{};

				vox::Resource image;
				image.kind = vox::ResourceKind::Texture;
				vox::parseDds(makeDx10Dds(99, block), image);			// DXGI_FORMAT_BC7_UNORM_SRGB

				check(image.format == vox::PixelFormat::BC7 && image.srgbHint, "DXGI BC7_UNORM_SRGB lost its sRGB flag");

				const Texture2D texture{ image, ColorSpace::Srgb, "texture-check/bc7" };
				check(levelInternalFormat(texture) == GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM, "BC7 sRGB is not stored as sRGB BPTC");
			}

			// ------------------- Cooked lookup ------------------
			{
				check(resources.cookedPathFor("textures/foo.png")
					== (resources.root() / ".cooked" / "textures" / "foo.png.dds").lexically_normal(),
					"cooked path does not mirror the source path");
				check(resources.relative(resources.cookedPathFor("textures/foo.png")) == ".cooked/textures/foo.png.dds",
					"cooked path is not inside the asset root");
				check(resources.cookedPathFor("textures/foo.png") != resources.cookedPathFor("textures/foo.tga"),
					"sources differing only by extension share a cooked file");
				check(resources.relative(resources.cookedPathFor("models/bar.FBX")) == ".cooked/models/bar.FBX.vmdl",
					"cooked model path is not <source>.vmdl");
			}

			// ------------------- Cooked model -------------------
			//
			// Not a texture, but the .vmdl reader sits beside parseDds and has
			// no other startup check. One triangle, laid out as ModelCompiler
			// writes it.
			{
				std::vector<unsigned char> file;
				const auto put = [&file](const void* data, std::size_t size)
				{
					const auto* bytes = static_cast<const unsigned char*>(data);
					file.insert(file.end(), bytes, bytes + size);
				};
				const auto u32 = [&put](std::uint32_t v) { put(&v, sizeof(v)); };

				const float positions[9] = { 0, 0, 0,  1, 0, 0,  0, 1, 0 };
				const float normals[9] = { 0, 0, 1,  0, 0, 1,  0, 0, 1 };
				const float uvs[6] = { 0, 1,  1, 1,  0, 0 };

				u32(0x4C444D56);		// "VMDL"
				u32(1);					// version
				u32(1);					// meshes
				u32(3);					// vertices
				u32(3);					// indices
				put(positions, sizeof(positions));
				put(normals, sizeof(normals));
				put(uvs, sizeof(uvs));
				u32(0); u32(1); u32(2);

				const auto modelThrows = [](const std::vector<unsigned char>& bytes)
				{
					vox::Resource model;
					try { vox::parseModel(bytes, model); }
					catch (const std::exception&) { return true; }
					return false;
				};

				vox::Resource model;
				try
				{
					vox::parseModel(file, model);
					check(model.meshes.size() == 1 && model.meshes[0].indices.size() == 3
						&& model.meshes[0].positions[1] == glm::vec3(1, 0, 0) && model.meshes[0].uv[2] == glm::vec2(0, 0),
						"cooked model round trip lost data");
				}
				catch (const std::exception& e)
				{
					std::cerr << "[texture-check] cooked model: " << e.what() << "\n";
					check(false, "valid cooked model was rejected");
				}

				std::vector<unsigned char> truncated = file;
				truncated.pop_back();
				check(modelThrows(truncated), "truncated cooked model was accepted");

				std::vector<unsigned char> badIndex = file;
				badIndex[badIndex.size() - 4] = 3;
				check(modelThrows(badIndex), "cooked model index past its vertices was accepted");
			}

			// ------------------- Manager ------------------------
			{
				TextureManager manager{ resources };

				std::cout << "[texture-check] the next two [TextureManager] lines are expected\n";

				const TextureHandle missing = manager.acquire("__texture_check__/does_not_exist.png", ColorSpace::Srgb);
				check(missing == manager.missing(), "a missing file did not resolve to the missing texture");
				check(manager.acquire("../outside_the_asset_root.png", ColorSpace::Srgb) == manager.missing(),
					"a path escaping the asset root did not resolve to the missing texture");

				const std::array<unsigned char, 4> pixel{ 10, 20, 30, 255 };

				const TextureHandle a = manager.add(Texture2D{ 1, 1, vox::PixelFormat::RGBA8, pixel,
					ColorSpace::Srgb, "texture-check/a", false });

				manager.release(a);

				const TextureHandle b = manager.add(Texture2D{ 1, 1, vox::PixelFormat::RGBA8, pixel,
					ColorSpace::Srgb, "texture-check/b", false });

				check(b.index_ == a.index_, "freed slot was not reused");
				check(!manager.isAlive(a), "released handle is still alive");
				check(manager.isAlive(b), "new handle is not alive");
				check(manager.get(a).id() == manager.get(manager.missing()).id(),
					"stale handle resolved to the texture that reused its slot");

				manager.release(manager.white());
				check(manager.isAlive(manager.white()), "the built-in white texture could be released");
				check(!TextureHandle{}.isValid(), "a default handle claims to be valid");

				const std::size_t before = manager.liveCount();
				check(manager.collectUnused({}) == 0 && manager.liveCount() == before,
					"collectUnused freed a built-in or code-owned texture");
			}

			// ------------------- Samplers -----------------------
			{
				SamplerCache samplers;

				const Sampler& first = samplers.get(SamplerDesc::trilinearRepeat());
				const Sampler& second = samplers.get(SamplerDesc::trilinearRepeat());
				check(&first == &second, "equal sampler descriptions did not share a sampler");

				SamplerDesc extreme = SamplerDesc::trilinearRepeat();
				extreme.anisotropy_ = 1000.f;
				check(samplers.get(extreme).desc().anisotropy_ <= samplers.maxAnisotropy(),
					"anisotropy not clamped to the driver maximum");

				check(samplers.get(SamplerDesc::pointClamp()).desc().anisotropy_ == 1.f,
					"unfiltered sampler kept anisotropy");
			}
		}
		catch (const std::exception& e)
		{
			std::cerr << "[texture-check] FAILED: unexpected exception: " << e.what() << "\n";
			++failures;
		}

		GLenum error = glGetError();
		check(error == GL_NO_ERROR, "a texture operation raised a GL error");

		while (error != GL_NO_ERROR)
			error = glGetError();

		std::cout << (failures == 0
			? "[texture-check] all checks passed\n"
			: "[texture-check] SOME CHECKS FAILED\n");

		return failures == 0;
	}
}
