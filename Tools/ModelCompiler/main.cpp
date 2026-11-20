/*************************************************************************************************************
\file       main.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      ModelCompiler entry point: cooks source models into the runtime's .vmdl format.
            Usage: ModelCompiler <assetRoot> [--force] [--file <relative path>]... With no --file, every
            obj/fbx/gltf/glb/dae under the root is cooked (dot folders skipped) and cooked models whose source
            is gone are removed. Exits non-zero if any model fails, which fails the CookAssets build step.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "model_compiler.hpp"

// Windows
#include <Windows.h>

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
			Prints the command-line usage to stdout.
		*/
	void printUsage()
	{
		std::printf("usage: ModelCompiler <assetRoot> [--force] [--file <relative path>]...\n"
			"  --force  re-cook models that are already up to date\n"
			"  --file   cook only this source, relative to the root; repeatable\n");
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
}

/*!
	@brief
		Parses the arguments, cooks the requested models and removes orphaned cooked files.
	@param argc
		Number of command-line arguments
	@param argv
		The command-line arguments (wide strings)
	@return
		0 on success, 1 if any model failed, 2 on a usage error
	*/
int wmain(int argc, wchar_t* argv[])
{
	std::filesystem::path root;
	std::vector<std::filesystem::path> only;
	tools::model::CookOptions options;

	for (int i = 1; i < argc; ++i)
	{
		const std::wstring_view arg = argv[i];

		if (arg == L"--force")
			options.force_ = true;
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
			std::fprintf(stderr, "ModelCompiler: error: unexpected argument '%ls'\n", argv[i]);
			printUsage();
			return 2;
		}
	}

	if (root.empty() || !std::filesystem::is_directory(root))
	{
		std::fprintf(stderr, "ModelCompiler: error: asset root '%ls' is not a folder\n", root.c_str());
		printUsage();
		return 2;
	}

	options.toolWriteTime_ = toolWriteTime();

	const tools::model::ModelCompiler compiler{ root, options };

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
			const tools::model::CookReport report = compiler.cook(source);

			if (report.skipped_)
			{
				++skipped;
				continue;
			}

			++cooked;

			std::printf("[ModelCompiler] %s -> %zu meshes, %zu vertices, %zu triangles, %.1f KB\n",
				name.c_str(), report.meshes_, report.vertices_, report.triangles_,
				static_cast<double>(report.cookedBytes_) / 1024.0);
		}
		catch (const std::exception& e)
		{
			++failed;
			std::fprintf(stderr, "ModelCompiler: error: %s: %s\n", name.c_str(), e.what());
		}
	}

	if (only.empty())
	{
		try
		{
			for (const auto& removed : compiler.removeOrphans())
				std::printf("[ModelCompiler] removed stale %s\n", removed.c_str());
		}
		catch (const std::exception& e)
		{
			std::fprintf(stderr, "ModelCompiler: warning: could not clean .cooked: %s\n", e.what());
		}
	}

	const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

	std::printf("[ModelCompiler] %d cooked, %d up to date, %d failed (%.1f s)\n", cooked, skipped, failed, seconds);

	return failed == 0 ? 0 : 1;
}
