/*************************************************************************************************************
\file       file_io.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Small header-only filesystem helpers: listing files by extension, reading an optional file,
            writing a text file and checking a directory exists.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// stds
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>


namespace util::file_io
{
	/*!
		@brief
			Looks for all files with a given extension (non-recursive).
		@param dir
			Directory to look into
		@param extension
			Extension to look for
		@return
			All paths with provided extension
		*/
	inline std::vector<std::filesystem::path> lookForFiles(const std::string& dir, const std::string& extension)
	{
		std::vector<std::filesystem::path> paths;

		for (const auto& entry : std::filesystem::directory_iterator(dir))
		{
			if (entry.is_regular_file() && entry.path().extension() == extension)
				paths.push_back(entry.path());
		}

		return paths;
	}

	/*!
		@brief
			Reads a whole file, treating absence as empty rather than as an error.

			For optional data files, where "not there" is a normal configuration
			and not something to complain about.
		@param path
			File to read
		@return
			File contents, or an empty string when it does not exist or will not
			open
		*/
	inline std::string readFileIfExists(const std::filesystem::path& path)
	{
		std::error_code ec;

		if (!std::filesystem::exists(path, ec) || ec)
			return {};

		std::ifstream in(path, std::ios::binary);
		if (!in)
			return {};

		return std::string(std::istreambuf_iterator<char>(in),
						   std::istreambuf_iterator<char>());
	}

	/*!
		@brief
			Writes to given path.
		@param path
			Path to write to
		@param toWrite
			Content to write
		*/
	inline void writeToFile(std::filesystem::path path, const std::string& toWrite)
	{
		std::ofstream inFile(path);
		if (inFile)
		{
			std::cerr << "Failed to open the path: " << path << std::endl;
			return;
		}

		inFile << toWrite;
	}

	/*!
		@brief
			Checks if a directory exists.
		@param dir
			Directory to check
		@return
			true when the path exists and is a directory
		*/
	inline bool dirExists(const std::string& dir)
	{
		return std::filesystem::exists(dir) && std::filesystem::is_directory(dir);
	}
}
