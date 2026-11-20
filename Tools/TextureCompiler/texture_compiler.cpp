/*************************************************************************************************************
\file       texture_compiler.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Implements TextureCompiler with DirectXTex: load, convert to the kind's working format,
            renormalise normals, generate mips, BCn-compress in parallel strips and write a DX10 DDS through a
            .tmp file and a rename; also the freshness check and orphan cleanup.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "texture_compiler.hpp"
#include "texture_settings.hpp"

// DirectXTex
#include <DirectXTex.h>

// Windows
#include <Windows.h>

// stds
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>


namespace tools::texture
{
	namespace
	{
		using namespace DirectX;

		// Must match kCookedFolder in Engine/Source/resources/ResourceManager.cpp.
		constexpr const wchar_t* kCookedFolder = L".cooked";

		// Largest edge the engine accepts (kMaxTextureEdge in ResourceManager.cpp).
		constexpr std::size_t kMaxTextureEdge = 16384;

		// BCn works on 4x4 texel blocks.
		constexpr std::size_t kBlockEdge = 4;

		/*!
			@brief
				Formats an HRESULT as its hex code plus the system's description of it.
			@param hr
				The result code
			@return
				E.g. "0x80070002 The system cannot find the file specified"
			*/
		std::string hresultText(HRESULT hr)
		{
			char code[16]{};
			std::snprintf(code, sizeof(code), "0x%08X", static_cast<unsigned>(hr));

			char* message = nullptr;
			const DWORD length = FormatMessageA(
				FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
				nullptr, static_cast<DWORD>(hr), 0, reinterpret_cast<LPSTR>(&message), 0, nullptr);

			std::string text = code;

			if (length > 0 && message)
			{
				std::string description{ message, length };

				while (!description.empty() && (description.back() == '\n' || description.back() == '\r' || description.back() == '.'))
					description.pop_back();

				text += " " + description;
			}

			LocalFree(message);

			return text;
		}

		/*!
			@brief
				Throws std::runtime_error naming the step if an HRESULT failed.
			@param hr
				The result code
			@param step
				The step's name, for the error message
			*/
		void check(HRESULT hr, const char* step)
		{
			if (FAILED(hr))
				throw std::runtime_error(std::string{ step } + " failed (" + hresultText(hr) + ")");
		}

		/*!
			@brief
				Names one of the cooked DXGI formats for the report.
			@param format
				The DXGI format
			@return
				Its DXGI name without the prefix, or "unknown"
			*/
		const char* formatName(DXGI_FORMAT format)
		{
			switch (format)
			{
			case DXGI_FORMAT_BC1_UNORM_SRGB:	return "BC1_UNORM_SRGB";
			case DXGI_FORMAT_BC3_UNORM_SRGB:	return "BC3_UNORM_SRGB";
			case DXGI_FORMAT_BC4_UNORM:			return "BC4_UNORM";
			case DXGI_FORMAT_BC5_UNORM:			return "BC5_UNORM";
			case DXGI_FORMAT_BC6H_UF16:			return "BC6H_UF16";
			case DXGI_FORMAT_BC7_UNORM_SRGB:	return "BC7_UNORM_SRGB";
			default:							return "unknown";
			}
		}

		/*!
			@brief
				Picks the cooked block-compressed format for a texture.

				One per slide of the lecture: BC7 (32) / BC1 (26) / BC3 (27) for colour, BC5 (29) for
				normals, BC4 (30) for single channels, BC6H (31) for HDR.
			@param settings
				The texture's resolved settings
			@return
				The target DXGI format
			*/
		DXGI_FORMAT targetFormat(const TextureSettings& settings)
		{
			switch (settings.kind_)
			{
			case TextureKind::Normal:	return DXGI_FORMAT_BC5_UNORM;
			case TextureKind::Mask:		return DXGI_FORMAT_BC4_UNORM;
			case TextureKind::Hdr:		return DXGI_FORMAT_BC6H_UF16;
			case TextureKind::Albedo:
			default:
				switch (settings.albedoFormat_)
				{
				case AlbedoFormat::BC1:	return DXGI_FORMAT_BC1_UNORM_SRGB;
				case AlbedoFormat::BC3:	return DXGI_FORMAT_BC3_UNORM_SRGB;
				case AlbedoFormat::BC7:
				default:				return DXGI_FORMAT_BC7_UNORM_SRGB;
				}
			}
		}

		/*!
			@brief
				Returns a path's extension in lower case.
			@param path
				The path to inspect
			@return
				The lower-case extension, including the dot
			*/
		std::wstring lowerExtension(const std::filesystem::path& path)
		{
			std::wstring ext = path.extension().wstring();
			std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
			return ext;
		}

		/*!
			@brief
				Loads a source image (HDR, TGA or WIC), ignoring sRGB metadata.

				Throws unless it is a single 2D image within the engine's edge limit.
			@param source
				The image file
			@return
				The loaded image
			*/
		ScratchImage load(const std::filesystem::path& source)
		{
			ScratchImage image;
			const std::wstring ext = lowerExtension(source);

			// sRGB metadata in the file is ignored: the texture's kind decides
			// its colour space, the same rule the runtime follows.
			if (ext == L".hdr")
				check(LoadFromHDRFile(source.c_str(), nullptr, image), "HDR load");
			else if (ext == L".tga")
				check(LoadFromTGAFile(source.c_str(), TGA_FLAGS_IGNORE_SRGB, nullptr, image), "TGA load");
			else
				check(LoadFromWICFile(source.c_str(), WIC_FLAGS_FORCE_RGB | WIC_FLAGS_IGNORE_SRGB, nullptr, image), "WIC load");

			const TexMetadata& meta = image.GetMetadata();

			if (meta.dimension != TEX_DIMENSION_TEXTURE2D || meta.arraySize != 1 || meta.IsCubemap())
				throw std::runtime_error("only single 2D images can be cooked");

			if (meta.width > kMaxTextureEdge || meta.height > kMaxTextureEdge)
				throw std::runtime_error("larger than the engine's " + std::to_string(kMaxTextureEdge) + " texel limit");

			return image;
		}

		/*!
			@brief
				Converts the base level in place, if it is not already in the format.

				The filter's default turns RGB into luminance when the target has one channel.
			@param image
				The image to convert
			@param format
				The working format
			*/
		void convert(ScratchImage& image, DXGI_FORMAT format)
		{
			if (image.GetMetadata().format == format)
				return;

			ScratchImage converted;
			check(Convert(*image.GetImage(0, 0, 0), format, TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, converted), "format conversion");
			image = std::move(converted);
		}

		/*!
			@brief
				Renormalises every normal in every level, optionally flipping green.

				Normal maps are unit vectors encoded as n * 0.5 + 0.5. Averaging for mips shortens them and
				block error nudges them, so every level is renormalised. Z is clamped to the outward
				hemisphere, because BC5 drops it and the shader rebuilds it as +sqrt(1 - X^2 - Y^2).
			@param image
				The float normal map, replaced in place
			@param flipGreen
				Negate Y (DirectX-style -Y source to OpenGL +Y)
			*/
		void renormalize(ScratchImage& image, bool flipGreen)
		{
			ScratchImage result;

			check(TransformImage(image.GetImages(), image.GetImageCount(), image.GetMetadata(),
				[flipGreen](XMVECTOR* outPixels, const XMVECTOR* inPixels, std::size_t width, std::size_t)
				{
					for (std::size_t i = 0; i < width; ++i)
					{
						XMFLOAT4 c;
						XMStoreFloat4(&c, inPixels[i]);

						float x = c.x * 2.f - 1.f;
						float y = c.y * 2.f - 1.f;
						float z = std::max(c.z * 2.f - 1.f, 0.f);

						if (flipGreen)
							y = -y;

						const float length = std::sqrt(x * x + y * y + z * z);

						if (length < 1e-6f)
						{
							x = 0.f;
							y = 0.f;
							z = 1.f;
						}
						else
						{
							x /= length;
							y /= length;
							z /= length;
						}

						outPixels[i] = XMVectorSet(x * .5f + .5f, y * .5f + .5f, z * .5f + .5f, 1.f);
					}
				}, result), "normal renormalisation");

			image = std::move(result);
		}

		/*!
			@brief
				Checks an RGBA8 image for alpha values other than 0 and 255.

				BC1 keeps one bit of alpha. Anything between 0 and 255 is rounded at TEX_THRESHOLD_DEFAULT,
				which usually is not what the artist meant.
			@param rgba8
				The RGBA8 base level
			@return
				True if any texel has partial alpha
			*/
		bool hasPartialAlpha(const Image& rgba8)
		{
			for (std::size_t y = 0; y < rgba8.height; ++y)
			{
				const std::uint8_t* row = rgba8.pixels + y * rgba8.rowPitch;

				for (std::size_t x = 0; x < rgba8.width; ++x)
				{
					const std::uint8_t alpha = row[x * 4 + 3];

					if (alpha != 0 && alpha != 255)
						return true;
				}
			}

			return false;
		}

		/*!
			@brief
				Compresses every mip level on every core.

				Each BCn block is encoded independently, so a level is cut into horizontal strips on block
				boundaries, the strips are compressed on worker threads and their block rows are copied into
				place. The result is byte-identical to compressing the level whole. This replaces
				DirectXTex's OpenMP path (see Vendor/CMakeLists.txt). Throws on the first failed strip.
			@param source
				The uncompressed mip chain
			@param format
				The BCn target format
			@param flags
				DirectXTex compression flags (e.g. BC7 quick)
			@return
				The compressed mip chain
			*/
		ScratchImage compressAllLevels(const ScratchImage& source, DXGI_FORMAT format, TEX_COMPRESS_FLAGS flags)
		{
			const TexMetadata& meta = source.GetMetadata();

			ScratchImage compressed;
			check(compressed.Initialize2D(format, meta.width, meta.height, 1, meta.mipLevels), "compressed storage");

			const std::size_t workers = std::max<std::size_t>(1, std::thread::hardware_concurrency());

			struct Strip
			{
				std::size_t level_;
				std::size_t firstBlockRow_;
				std::size_t blockRows_;
			};

			std::vector<Strip> strips;

			for (std::size_t level = 0; level < meta.mipLevels; ++level)
			{
				const std::size_t blockRows = (source.GetImage(level, 0, 0)->height + kBlockEdge - 1) / kBlockEdge;

				// About four strips per worker keeps the big levels balanced.
				const std::size_t perStrip = std::max<std::size_t>(1, blockRows / (workers * 4));

				for (std::size_t row = 0; row < blockRows; row += perStrip)
					strips.push_back(Strip{ level, row, std::min(perStrip, blockRows - row) });
			}

			std::atomic<std::size_t> next{ 0 };
			std::mutex errorMutex;
			std::string error;

			const auto work = [&]()
			{
				for (std::size_t i = next++; i < strips.size(); i = next++)
				{
					const Strip& strip = strips[i];
					const Image& level = *source.GetImage(strip.level_, 0, 0);

					Image part = level;
					part.pixels = level.pixels + strip.firstBlockRow_ * kBlockEdge * level.rowPitch;
					part.height = std::min(strip.blockRows_ * kBlockEdge, level.height - strip.firstBlockRow_ * kBlockEdge);
					part.slicePitch = part.height * part.rowPitch;

					ScratchImage encoded;
					const HRESULT hr = Compress(part, format, flags, TEX_THRESHOLD_DEFAULT, encoded);

					const Image* from = FAILED(hr) ? nullptr : encoded.GetImage(0, 0, 0);
					const Image* to = compressed.GetImage(strip.level_, 0, 0);

					if (!from || from->rowPitch != to->rowPitch
						|| (strip.firstBlockRow_ * to->rowPitch) + from->slicePitch > to->slicePitch)
					{
						const std::lock_guard lock{ errorMutex };

						if (error.empty())
							error = FAILED(hr) ? "BC compression failed (" + hresultText(hr) + ")"
								: "BC compression produced a strip of unexpected size";

						next = strips.size();
						return;
					}

					std::memcpy(to->pixels + strip.firstBlockRow_ * to->rowPitch, from->pixels, from->slicePitch);
				}
			};

			{
				std::vector<std::jthread> threads;
				threads.reserve(workers - 1);

				for (std::size_t t = 1; t < workers; ++t)
					threads.emplace_back(work);

				work();
			}

			if (!error.empty())
				throw std::runtime_error(error);

			return compressed;
		}

		/*!
			@brief
				Reads a file's last write time without throwing.
			@param path
				The file to inspect
			@return
				Its write time, or file_time_type::min() if it cannot be read (e.g. it does not exist)
			*/
		std::filesystem::file_time_type writeTime(const std::filesystem::path& path)
		{
			std::error_code ec;
			const auto time = std::filesystem::last_write_time(path, ec);
			return ec ? std::filesystem::file_time_type::min() : time;
		}
	}

	/*!
		@brief
			Creates a compiler over an asset root.
		@param assetRoot
			The asset folder; stored canonicalised
		@param options
			Force, quick and tool-timestamp options
		*/
	TextureCompiler::TextureCompiler(std::filesystem::path assetRoot, CookOptions options)
		:
		root_{ std::filesystem::weakly_canonical(assetRoot) },
		options_{ options }
	{}

	/*!
		@brief
			Lists every source image under the root, skipping dot folders.
		@return
			The source image paths, sorted
		*/
	std::vector<std::filesystem::path> TextureCompiler::findSources() const
	{
		std::vector<std::filesystem::path> sources;

		for (auto it = std::filesystem::recursive_directory_iterator(root_); it != std::filesystem::recursive_directory_iterator(); ++it)
		{
			if (it->is_directory() && it->path().filename().wstring().starts_with(L'.'))
			{
				it.disable_recursion_pending();
				continue;
			}

			if (it->is_regular_file() && isSourceImage(it->path()))
				sources.push_back(it->path());
		}

		std::sort(sources.begin(), sources.end());

		return sources;
	}

	/*!
		@brief
			Maps a source image to its cooked output path.
		@param source
			An image under the asset root
		@return
			<root>/.cooked/<relative path>.dds
		*/
	std::filesystem::path TextureCompiler::outputPathFor(const std::filesystem::path& source) const
	{
		std::filesystem::path output = root_ / kCookedFolder / std::filesystem::weakly_canonical(source).lexically_relative(root_);
		output += L".dds";
		return output;
	}

	/*!
		@brief
			Checks whether an image's cooked output is newer than its source, sidecar and the compiler.
		@param source
			The source image
		@param output
			Its cooked output path
		@return
			True if the cook can be skipped (never when forced or when the output is missing)
		*/
	bool TextureCompiler::isUpToDate(const std::filesystem::path& source, const std::filesystem::path& output) const
	{
		if (options_.force_ || !std::filesystem::exists(output))
			return false;

		const auto cooked = writeTime(output);

		return cooked >= writeTime(source)
			&& cooked >= writeTime(sidecarPath(source))		// min() when there is no sidecar
			&& cooked >= options_.toolWriteTime_;
	}

	/*!
		@brief
			Cooks one image to a block-compressed DDS, unless its output is already up to date.

			Throws std::runtime_error with the failing step on any error; a failed write removes the
			temporary file so no partial output is left.
		@param source
			An image under the asset root
		@return
			Kind, format, size, level count and byte counts, or skipped_ = true
		*/
	CookReport TextureCompiler::cook(const std::filesystem::path& source) const
	{
		const auto relative = std::filesystem::weakly_canonical(source).lexically_relative(root_);

		if (relative.empty() || relative.is_absolute() || *relative.begin() == L"..")
			throw std::runtime_error("source is outside the asset root");

		const TextureSettings settings = resolveSettings(source);
		const std::filesystem::path output = outputPathFor(source);

		CookReport report;
		report.kind_ = kindName(settings.kind_);

		if (isUpToDate(source, output))
		{
			report.skipped_ = true;
			return report;
		}

		ScratchImage image = load(source);

		// ------------------- Working format -----------------
		switch (settings.kind_)
		{
		case TextureKind::Albedo:
			// The bytes already are sRGB-encoded colour. Relabel rather than
			// convert, which would encode them a second time.
			convert(image, DXGI_FORMAT_R8G8B8A8_UNORM);
			image.OverrideFormat(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
			break;

		case TextureKind::Normal:
			convert(image, DXGI_FORMAT_R32G32B32A32_FLOAT);
			renormalize(image, settings.flipGreen_);
			break;

		case TextureKind::Mask:
			convert(image, DXGI_FORMAT_R8_UNORM);
			break;

		case TextureKind::Hdr:
			convert(image, DXGI_FORMAT_R32G32B32A32_FLOAT);
			break;
		}

		const DXGI_FORMAT format = targetFormat(settings);

		if (format == DXGI_FORMAT_BC1_UNORM_SRGB && hasPartialAlpha(*image.GetImage(0, 0, 0)))
			std::printf("[TextureCompiler] warning: %s has partial alpha, but BC1 stores 1 bit;"
				" use format = bc7 or bc3\n", relative.generic_string().c_str());

		// ------------------- Mips ---------------------------
		//
		// Box filter for power-of-two sizes, linear otherwise (DirectXTex's
		// default). sRGB input is averaged in linear light, so distant albedo
		// does not darken.
		if (settings.mips_ && (image.GetMetadata().width > 1 || image.GetMetadata().height > 1))
		{
			const TEX_FILTER_FLAGS filter = TEX_FILTER_DEFAULT | TEX_FILTER_FORCE_NON_WIC
				| (settings.kind_ == TextureKind::Albedo ? TEX_FILTER_SRGB : TEX_FILTER_DEFAULT);

			ScratchImage chain;
			check(GenerateMipMaps(*image.GetImage(0, 0, 0), filter, 0, chain), "mip generation");
			image = std::move(chain);

			if (settings.kind_ == TextureKind::Normal)
				renormalize(image, false);

			if (settings.alphaCutoff_ > 0.f && !image.IsAlphaAllOpaque())
			{
				ScratchImage covered;
				check(covered.Initialize(image.GetMetadata()), "coverage storage");
				check(ScaleMipMapsAlphaForCoverage(image.GetImages(), image.GetMetadata().mipLevels, image.GetMetadata(),
					0, settings.alphaCutoff_, covered), "alpha coverage");
				image = std::move(covered);
			}
		}

		// ------------------- Compress -----------------------
		const TEX_COMPRESS_FLAGS flags = options_.quick_ ? TEX_COMPRESS_BC7_QUICK : TEX_COMPRESS_DEFAULT;
		const ScratchImage compressed = compressAllLevels(image, format, flags);

		// ------------------- Write --------------------------
		//
		// DX10 header always, so the sRGB flag is explicit. Written beside the
		// output and renamed over it, so nothing ever reads half a file.
		std::filesystem::create_directories(output.parent_path());

		std::filesystem::path temporary = output;
		temporary += L".tmp";

		const HRESULT saved = SaveToDDSFile(compressed.GetImages(), compressed.GetImageCount(), compressed.GetMetadata(),
			DDS_FLAGS_FORCE_DX10_EXT, temporary.c_str());

		if (FAILED(saved))
		{
			std::error_code ignored;
			std::filesystem::remove(temporary, ignored);
			check(saved, "DDS write");
		}

		std::filesystem::rename(temporary, output);

		// ------------------- Report -------------------------
		const TexMetadata& meta = compressed.GetMetadata();
		const std::size_t bytesPerTexel = settings.kind_ == TextureKind::Hdr ? 8 : 4;

		report.format_	= formatName(format);
		report.width_	= meta.width;
		report.height_	= meta.height;
		report.levels_	= meta.mipLevels;
		report.cookedBytes_ = compressed.GetPixelsSize();

		for (std::size_t level = 0; level < meta.mipLevels; ++level)
		{
			const Image* mip = compressed.GetImage(level, 0, 0);
			report.uncompressedBytes_ += mip->width * mip->height * bytesPerTexel;
		}

		return report;
	}

	/*!
		@brief
			Deletes cooked .dds files whose source image is gone, and this tool's stray .dds.tmp files.
		@return
			Paths removed, relative to the root
		*/
	std::vector<std::string> TextureCompiler::removeOrphans() const
	{
		std::vector<std::string> removed;
		const std::filesystem::path cookedRoot = root_ / kCookedFolder;

		if (!std::filesystem::is_directory(cookedRoot))
			return removed;

		std::vector<std::filesystem::path> stale;

		for (const auto& entry : std::filesystem::recursive_directory_iterator(cookedRoot))
		{
			if (!entry.is_regular_file())
				continue;

			const std::wstring ext = lowerExtension(entry.path());

			// .cooked is shared with ModelCompiler: only .dds files and their
			// temporaries belong to this tool.
			if (ext == L".tmp")
			{
				if (lowerExtension(entry.path().stem()) == L".dds")
					stale.push_back(entry.path());
				continue;
			}

			if (ext != L".dds")
				continue;

			// .cooked/textures/foo.png.dds -> textures/foo.png
			std::filesystem::path source = root_ / entry.path().lexically_relative(cookedRoot);
			source.replace_extension();

			if (!isSourceImage(source) || !std::filesystem::exists(source))
				stale.push_back(entry.path());
		}

		for (const auto& path : stale)
		{
			std::error_code ec;

			if (std::filesystem::remove(path, ec))
				removed.push_back(path.lexically_relative(root_).generic_string());
		}

		return removed;
	}
}
