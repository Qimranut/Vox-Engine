/*************************************************************************************************************
\file       model_compiler.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares ModelCompiler, which cooks source models under an asset root into .vmdl files, plus its
            cook options and per-model report.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// stds
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>


namespace tools::model
{
	struct CookOptions
	{
		// Re-cook even when the output is newer than its inputs.
		bool								force_			{ false };

		// The compiler's own executable. A rebuilt compiler re-cooks everything,
		// so an importer or format change is never hidden behind an up-to-date check.
		std::filesystem::file_time_type		toolWriteTime_	{};
	};

	struct CookReport
	{
		bool			skipped_		{ false };		// output was already up to date
		std::size_t		meshes_			{ 0 };
		std::size_t		vertices_		{ 0 };
		std::size_t		triangles_		{ 0 };
		std::size_t		cookedBytes_	{ 0 };
	};

	/**
	 * Cooks source models under an asset root into .vmdl files.
	 *
	 * For each model: import with Assimp (triangulate, smooth normals, join
	 * identical vertices, bake the node hierarchy into the vertices, flip UVs
	 * to the top-left origin every texture uses) -> write positions, normals,
	 * UVs and indices per mesh -> rename over the output atomically. Output
	 * goes to <root>/.cooked/<relative source path>.vmdl, which is exactly
	 * where vox::ResourceManager looks; vox::parseModel reads it.
	 *
	 * File layout, little-endian:
	 *   "VMDL", u32 version (1), u32 meshCount, then per mesh
	 *   u32 vertexCount, u32 indexCount,
	 *   float3 positions[vertexCount], float3 normals[vertexCount],
	 *   float2 uvs[vertexCount], u32 indices[indexCount]
	 *
	 */
	class ModelCompiler
	{
	public:

		/*!
			@brief
				Creates a compiler over an asset root.
			@param assetRoot
				The asset folder; stored canonicalised
			@param options
				Force and tool-timestamp options for the up-to-date check
			*/
		ModelCompiler(std::filesystem::path assetRoot, CookOptions options);

		/*!
			@brief
				Lists every source model under the root.
			@return
				Every source model under the root, sorted, skipping dot folders
			*/
		std::vector<std::filesystem::path> findSources() const;

		/*!
			@brief
				Maps a source model to its cooked output path.
			@param source
				A model under the asset root
			@return
				<root>/.cooked/<relative path>.vmdl
			*/
		std::filesystem::path outputPathFor(const std::filesystem::path& source) const;

		/*!
			@brief
				Cooks one model, unless its output is newer than the model and the compiler itself.

				Throws std::runtime_error on any error. A failed cook never leaves a partial output behind.
			@param source
				A model under the asset root
			@return
				What was written, or skipped_ = true
			*/
		CookReport cook(const std::filesystem::path& source) const;

		/*!
			@brief
				Deletes cooked models whose source no longer exists, and this tool's stray temporaries.

				Other cookers' files in .cooked are left alone.
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
				Checks whether a model's cooked output is at least as new as the source and the compiler.
			@param source
				The source model
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
