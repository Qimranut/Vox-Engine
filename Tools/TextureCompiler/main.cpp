/*************************************************************************************************************
\file       main.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      TextureCompiler entry point: cooks source images into block-compressed DDS files.
            Usage: TextureCompiler <assetRoot> [--force] [--quick] [--file <relative path>]... With no --file,
            every png/jpg/jpeg/bmp/tga/hdr under the root is cooked (dot folders skipped) and cooked files whose
            source is gone are removed. Exits non-zero if any texture fails, which fails the CookAssets step.
            The CookAssets build step passes --quick; run with --force (no --quick) for a full-quality cook.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "texture_compiler.hpp"

// Windows
#include <Windows.h>
#include <objbase.h>

// stds
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>


namespace
{
	/*!
		@brief
			Prints the command-line usage and the per-texture settings summary to stdout.
		*/
	void printUsage()
	{
		std::printf("usage: TextureCompiler <assetRoot> [--force] [--quick] [--file <relative path>]...\n"
			"  --force  re-cook textures that are already up to date\n"
			"  --quick  fastest BC7 mode only (what the build's CookAssets step uses)\n"
			"  --file   cook only this source, relative to the root; repeatable\n"
			"settings per texture: <source>.texmeta, or name suffixes _n (normal) / _mask _r _m _ao _h _sdf (mask)\n");
	}

	/*!
		@brief
			Reads the last write time of this executable, so a rebuilt compiler re-cooks everything.
		@return
			The executable's write time, or a default time if it cannot be read
		*/
	std::filesystem::file_time_type toolWriteTime()
	{
		std::wstring path(MAX_PATH, L'\0');

		for (;;)
		{
			const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));

			if (length == 0)
				return {};

			if (length < path.size())
			{
				path.resize(length);
				break;
			}

			path.resize(path.size() * 2);
		}

		std::error_code ec;
		const auto time = std::filesystem::last_write_time(path, ec);
		return ec ? std::filesystem::file_time_type{} : time;
	}

	/*!
		@brief
			Converts a byte count to mebibytes.
		@param bytes
			The byte count
		@return
			The size in MB (1024 * 1024 bytes)
		*/
	double megabytes(std::size_t bytes)
	{
		return static_cast<double>(bytes) / (1024.0 * 1024.0);
	}

	// Initialises COM for WIC on this thread and releases it on exit.
	class ComScope
	{
	public:
		/*!
			@brief
				Initialises COM (multithreaded) on this thread.
			*/
		ComScope() : hr_{ CoInitializeEx(nullptr, COINIT_MULTITHREADED) } {}

		/*!
			@brief
				Releases COM if the constructor initialised it.
			*/
		~ComScope() { if (SUCCEEDED(hr_)) CoUninitialize(); }

		ComScope(const ComScope&) = delete;
		ComScope& operator=(const ComScope&) = delete;

		/*!
			@brief
				Tells whether COM initialisation succeeded.
			@return
				True if WIC can be used on this thread
			*/
		bool ok() const { return SUCCEEDED(hr_); }

	private:
		HRESULT hr_;
	};
}

/*!
	@brief
		Parses the arguments, initialises COM, cooks the requested textures and removes orphaned cooked files.
	@param argc
		Number of command-line arguments
	@param argv
		The command-line arguments (wide strings)
	@return
		0 on success, 1 if any texture failed or COM could not start, 2 on a usage error
	*/
int wmain(int argc, wchar_t* argv[])
{
	std::filesystem::path root;
	std::vector<std::filesystem::path> only;
	tools::texture::CookOptions options;

	for (int i = 1; i < argc; ++i)
	{
		const std::wstring_view arg = argv[i];

		if (arg == L"--force")
			options.force_ = true;
		else if (arg == L"--quick")
			options.quick_ = true;
		else if (arg == L"--file" && i + 1 < argc)
			only.emplace_back(argv[++i]);
		else if (arg == L"--help" || arg == L"-h")
		{
			printUsage();
			return 0;
		}
		else if (!arg.starts_with(L"--") && root.empty())
			root = arg;
		else
		{
			std::fprintf(stderr, "TextureCompiler: error: unexpected argument '%ls'\n", argv[i]);
			printUsage();
			return 2;
		}
	}

	if (root.empty() || !std::filesystem::is_directory(root))
	{
		std::fprintf(stderr, "TextureCompiler: error: asset root '%ls' is not a folder\n", root.c_str());
		printUsage();
		return 2;
	}

	const ComScope com;

	if (!com.ok())
	{
		std::fprintf(stderr, "TextureCompiler: error: COM initialisation failed; WIC image loading is unavailable\n");
		return 1;
	}

	options.toolWriteTime_ = toolWriteTime();

	const tools::texture::TextureCompiler compiler{ root, options };

	std::vector<std::filesystem::path> sources;

	if (only.empty())
		sources = compiler.findSources();
	else
		for (const auto& relative : only)
			sources.push_back(compiler.root() / relative);

	const auto started = std::chrono::steady_clock::now();

	int cooked = 0;
	int skipped = 0;
	int failed = 0;

	for (const auto& source : sources)
	{
		const std::string name = source.lexically_relative(compiler.root()).generic_string();

		try
		{
			const tools::texture::CookReport report = compiler.cook(source);

			if (report.skipped_)
			{
				++skipped;
				continue;
			}

			++cooked;

			std::printf("[TextureCompiler] %s (%s) -> %s %zux%zu, %zu mips, %.2f MB -> %.2f MB (%.1f:1)\n",
				name.c_str(), report.kind_.c_str(), report.format_.c_str(), report.width_, report.height_, report.levels_,
				megabytes(report.uncompressedBytes_), megabytes(report.cookedBytes_),
				report.cookedBytes_ ? static_cast<double>(report.uncompressedBytes_) / static_cast<double>(report.cookedBytes_) : 0.0);
		}
		catch (const std::exception& e)
		{
			++failed;
			std::fprintf(stderr, "TextureCompiler: error: %s: %s\n", name.c_str(), e.what());
		}
	}

	if (only.empty())
	{
		try
		{
			for (const auto& removed : compiler.removeOrphans())
				std::printf("[TextureCompiler] removed stale %s\n", removed.c_str());
		}
		catch (const std::exception& e)
		{
			std::fprintf(stderr, "TextureCompiler: warning: could not clean .cooked: %s\n", e.what());
		}
	}

	const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

	std::printf("[TextureCompiler] %d cooked, %d up to date, %d failed (%.1f s)\n", cooked, skipped, failed, seconds);

	return failed == 0 ? 0 : 1;
}
