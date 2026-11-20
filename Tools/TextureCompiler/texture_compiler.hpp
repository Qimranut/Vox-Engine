/*************************************************************************************************************
\file       texture_compiler.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares TextureCompiler, which cooks source images under an asset root into block-compressed
            DDS files, plus its cook options and per-texture report.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// stds
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>


namespace tools::texture
{
	struct CookOptions
	{
		// Re-cook even when the output is newer than its inputs.
		bool								force_			{ false };

		// BC7 searches only its fastest mode. For iteration, not shipping.
		bool								quick_			{ false };

		// The compiler's own executable. A rebuilt compiler re-cooks everything,
		// so an encoder change is never hidden behind an up-to-date check.
		std::filesystem::file_time_type		toolWriteTime_	{};
	};

	struct CookReport
	{
		bool			skipped_			{ false };		// output was already up to date
		std::string		format_;							// DXGI name, e.g. BC7_UNORM_SRGB
		std::string		kind_;
		std::size_t		width_				{ 0 };
		std::size_t		height_				{ 0 };
		std::size_t		levels_				{ 0 };
		std::size_t		uncompressedBytes_	{ 0 };			// same chain as RGBA8 (RGBA16F for HDR)
		std::size_t		cookedBytes_		{ 0 };			// block data written to the DDS
	};

	/**
	 * Cooks source images under an asset root into block-compressed DDS files.
	 *
	 * For each image: load -> convert to the kind's working format -> (normal
	 * maps) renormalise -> generate the full mip chain in linear light -> BCn
	 * compress on every core -> write a DX10-header DDS atomically. Output goes
	 * to <root>/.cooked/<relative source path>.dds, which is exactly where
	 * vox::ResourceManager looks. Row 0 stays the top of the image.
	 *
	 */
	class TextureCompiler
	{
	public:

		/*!
			@brief
				Creates a compiler over an asset root.
			@param assetRoot
				The asset folder; stored canonicalised
			@param options
				Force, quick and tool-timestamp options
			*/
		TextureCompiler(std::filesystem::path assetRoot, CookOptions options);

		/*!
			@brief
				Lists every source image under the root.
			@return
				Every source image under the root, sorted, skipping dot folders
			*/
		std::vector<std::filesystem::path> findSources() const;

		/*!
			@brief
				Maps a source image to its cooked output path.
			@param source
				An image under the asset root
			@return
				<root>/.cooked/<relative path>.dds
			*/
		std::filesystem::path outputPathFor(const std::filesystem::path& source) const;

		/*!
			@brief
				Cooks one image, unless its output is newer than the image, its .texmeta sidecar and the compiler.

				Throws std::runtime_error with the failing step on any error. A failed cook never leaves a
				partial output behind.
			@param source
				An image under the asset root
			@return
				What was written, or skipped_ = true
			*/
		CookReport cook(const std::filesystem::path& source) const;

		/*!
			@brief
				Deletes cooked files whose source image no longer exists, and stray temporaries from an
				interrupted run.
			@return
				Paths removed, relative to the root
			*/
		std::vector<std::string> removeOrphans() const;

		/*!
			@brief
				Returns the canonical asset root.
			@return
				The asset root path
			*/
		const std::filesystem::path& root() const { return root_; }

	private:

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
		bool isUpToDate(const std::filesystem::path& source, const std::filesystem::path& output) const;

		std::filesystem::path	root_;
		CookOptions				options_;
	};
}
