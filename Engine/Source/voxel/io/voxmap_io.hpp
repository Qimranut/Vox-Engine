/*************************************************************************************************************
\file       voxmap_io.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares reading and writing of .voxmap files (level 0 of a voxel world plus its provenance).

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Voxel
#include "../types/material_table.hpp"
#include "../world/world.hpp"

// stds
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>


namespace voxel::voxmap
{
	/**
	 * Provenance recorded in a map so a loader can tell where it came
	 * from and whether the material table moved underneath it.
	 *
	 */
	struct Info
	{
		std::uint64_t	seed_			{ 0 };
		std::uint64_t	materialHash_	{ 0 };
		std::string		recipePath_;
	};

	/*!
		@brief
			Serialises level 0 of a world into .voxmap bytes.

			Dense sections whose voxels are all equal are written as uniform
			without touching the world.
		@param world
			World to serialise
		@param materials
			Material table whose names are stored
		@param info
			Provenance to record
		@return
			The file bytes
		*/
	std::vector<unsigned char> write(const World& world, const MaterialTable& materials, const Info& info);

	/*!
		@brief
			write() to disk through a .tmp file and an atomic rename, like SceneManager::save.

			Throws std::runtime_error on failure.
		@param path
			Destination .voxmap path
		@param world
			World to serialise
		@param materials
			Material table whose names are stored
		@param info
			Provenance to record
		*/
	void writeFile(const std::filesystem::path& path, const World& world, const MaterialTable& materials, const Info& info);

	struct ReadResult
	{
		std::unique_ptr<World>		world_;
		Info						info_;
		std::vector<std::string>	warnings_;	// unknown material names, stale table hash
	};

	/*!
		@brief
			Parses a map into a new world.

			File material names are remapped onto the runtime table; unknown
			names fall back to runtime id 1 with a warning. Throws
			std::runtime_error on a corrupt file.
		@param bytes
			The whole file
		@param materials
			Runtime material table
		@return
			The world, its provenance and any warnings
		*/
	ReadResult read(std::span<const unsigned char> bytes, const MaterialTable& materials);
}
