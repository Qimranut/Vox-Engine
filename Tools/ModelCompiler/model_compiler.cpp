/*************************************************************************************************************
\file       model_compiler.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Implements ModelCompiler: imports models with Assimp, writes the .vmdl layout through a .tmp file
            and a rename, checks cooks for freshness and removes orphaned cooked models.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "model_compiler.hpp"

// Assimp
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

// stds
#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <fstream>
#include <stdexcept>


namespace tools::model
{
	namespace
	{
		// Matches vox::ResourceManager's kCookedFolder and its model constants.
		constexpr const wchar_t*	kCookedFolder		= L".cooked";
		constexpr const wchar_t*	kCookedExtension	= L".vmdl";
		constexpr std::uint32_t		kMagic				= 'V' | ('M' << 8) | ('D' << 16) | ('L' << 24);
		constexpr std::uint32_t		kVersion			= 1;
		constexpr std::uint32_t		kMaxMeshes			= 4096;
		constexpr std::uint32_t		kMaxVertices		= 1u << 24;
		constexpr std::uint32_t		kMaxIndices			= 1u << 26;

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
			std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
			return ext;
		}

		/*!
			@brief
				Checks whether a path has a model extension this tool cooks.
			@param path
				The path to inspect
			@return
				True for obj, fbx, gltf, glb and dae
			*/
		bool isSourceModel(const std::filesystem::path& path)
		{
			const std::wstring ext = lowerExtension(path);
			return ext == L".obj" || ext == L".fbx" || ext == L".gltf" || ext == L".glb" || ext == L".dae";
		}

		/*!
			@brief
				Reads a file's last write time without throwing.
			@param path
				The file to inspect
			@return
				Its write time, or file_time_type::min() if it cannot be read
			*/
		std::filesystem::file_time_type writeTime(const std::filesystem::path& path)
		{
			std::error_code ec;
			const auto time = std::filesystem::last_write_time(path, ec);
			return ec ? std::filesystem::file_time_type::min() : time;
		}

		class Writer
		{
		public:
			/*!
				@brief
					Opens a binary output file, truncating it; throws if it cannot be opened.
				@param path
					The file to write
				*/
			explicit Writer(const std::filesystem::path& path) : out_{ path, std::ios::binary | std::ios::trunc }
			{
				if (!out_)
					throw std::runtime_error("cannot open output for writing");
			}

			/*!
				@brief
					Writes one 32-bit unsigned integer in native (little-endian) order.
				@param v
					The value to write
				*/
			void u32(std::uint32_t v) { raw(&v, sizeof(v)); }

			/*!
				@brief
					Writes one 32-bit float.
				@param v
					The value to write
				*/
			void f32(float v) { raw(&v, sizeof(v)); }

			/*!
				@brief
					Flushes the file and throws if any write failed.
				@return
					The number of bytes written
				*/
			std::size_t finish()
			{
				out_.flush();
				if (!out_)
					throw std::runtime_error("write failed");
				return bytes_;
			}

		private:
			/*!
				@brief
					Writes raw bytes and counts them.
				@param data
					The bytes to write
				@param size
					How many bytes
				*/
			void raw(const void* data, std::size_t size)
			{
				out_.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
				bytes_ += size;
			}

			std::ofstream	out_;
			std::size_t		bytes_	{ 0 };
		};
	}

	/*!
		@brief
			Creates a compiler over an asset root.
		@param assetRoot
			The asset folder; stored canonicalised
		@param options
			Force and tool-timestamp options for the up-to-date check
		*/
	ModelCompiler::ModelCompiler(std::filesystem::path assetRoot, CookOptions options)
		:
		root_{ std::filesystem::weakly_canonical(assetRoot) },
		options_{ options }
	{}

	/*!
		@brief
			Lists every source model under the root, skipping dot folders.
		@return
			The source model paths, sorted
		*/
	std::vector<std::filesystem::path> ModelCompiler::findSources() const
	{
		std::vector<std::filesystem::path> sources;

		for (auto it = std::filesystem::recursive_directory_iterator(root_); it != std::filesystem::recursive_directory_iterator(); ++it)
		{
			if (it->is_directory() && it->path().filename().wstring().starts_with(L'.'))
			{
				it.disable_recursion_pending();
				continue;
			}

			if (it->is_regular_file() && isSourceModel(it->path()))
				sources.push_back(it->path());
		}

		std::sort(sources.begin(), sources.end());

		return sources;
	}

	/*!
		@brief
			Maps a source model to its cooked output path.
		@param source
			A model under the asset root
		@return
			<root>/.cooked/<relative path>.vmdl
		*/
	std::filesystem::path ModelCompiler::outputPathFor(const std::filesystem::path& source) const
	{
		std::filesystem::path output = root_ / kCookedFolder / std::filesystem::weakly_canonical(source).lexically_relative(root_);
		output += kCookedExtension;
		return output;
	}

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
	bool ModelCompiler::isUpToDate(const std::filesystem::path& source, const std::filesystem::path& output) const
	{
		if (options_.force_ || !std::filesystem::exists(output))
			return false;

		const auto cooked = writeTime(output);

		return cooked >= writeTime(source) && cooked >= options_.toolWriteTime_;
	}

	/*!
		@brief
			Cooks one model to .vmdl, unless its output is already up to date.

			Throws std::runtime_error on any error; the temporary file is removed so no partial output is left.
		@param source
			A model under the asset root
		@return
			Mesh, vertex, triangle and byte counts written, or skipped_ = true
		*/
	CookReport ModelCompiler::cook(const std::filesystem::path& source) const
	{
		const auto relative = std::filesystem::weakly_canonical(source).lexically_relative(root_);

		if (relative.empty() || relative.is_absolute() || *relative.begin() == L"..")
			throw std::runtime_error("source is outside the asset root");

		if (!isSourceModel(source))
			throw std::runtime_error("not a model (obj, fbx, gltf, glb, dae)");

		const std::filesystem::path output = outputPathFor(source);

		CookReport report;

		if (isUpToDate(source, output))
		{
			report.skipped_ = true;
			return report;
		}

		// ------------------- Import -------------------------
		//
		// PreTransformVertices bakes the node hierarchy in, so each mesh is in
		// model space. FlipUVs puts UVs in the top-left origin every texture
		// uses: pixel rows are never flipped, because BC blocks cannot be.
		Assimp::Importer importer;
		const std::u8string utf8 = source.u8string();
		const aiScene* scene = importer.ReadFile(reinterpret_cast<const char*>(utf8.c_str()),
			aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_JoinIdenticalVertices
			| aiProcess_PreTransformVertices | aiProcess_ValidateDataStructure | aiProcess_FlipUVs);

		if (!scene || !scene->HasMeshes())
			throw std::runtime_error(std::string("import failed: ") + importer.GetErrorString());

		if (scene->mNumMeshes > kMaxMeshes)
			throw std::runtime_error("too many meshes");

		// ------------------- Write --------------------------
		//
		// Written beside the output and renamed over it, so nothing ever reads
		// half a file.
		std::filesystem::create_directories(output.parent_path());

		std::filesystem::path temporary = output;
		temporary += L".tmp";

		try
		{
			Writer out{ temporary };
			out.u32(kMagic);
			out.u32(kVersion);
			out.u32(scene->mNumMeshes);

			for (unsigned m = 0; m < scene->mNumMeshes; ++m)
			{
				const aiMesh& mesh = *scene->mMeshes[m];

				// Triangulate leaves point and line primitives as they are; the
				// renderer draws triangles only, so those faces are dropped.
				std::vector<std::uint32_t> indices;
				indices.reserve(static_cast<std::size_t>(mesh.mNumFaces) * 3u);
				for (unsigned f = 0; f < mesh.mNumFaces; ++f)
				{
					const aiFace& face = mesh.mFaces[f];
					if (face.mNumIndices == 3)
						indices.insert(indices.end(), face.mIndices, face.mIndices + 3);
				}

				if (mesh.mNumVertices == 0 || mesh.mNumVertices > kMaxVertices || indices.size() > kMaxIndices)
					throw std::runtime_error("mesh " + std::to_string(m) + " is empty or too large");

				out.u32(mesh.mNumVertices);
				out.u32(static_cast<std::uint32_t>(indices.size()));

				for (unsigned v = 0; v < mesh.mNumVertices; ++v)
				{
					const aiVector3D& p = mesh.mVertices[v];
					out.f32(p.x); out.f32(p.y); out.f32(p.z);
				}
				for (unsigned v = 0; v < mesh.mNumVertices; ++v)
				{
					const aiVector3D n = mesh.HasNormals() ? mesh.mNormals[v] : aiVector3D(0.f, 1.f, 0.f);
					out.f32(n.x); out.f32(n.y); out.f32(n.z);
				}
				for (unsigned v = 0; v < mesh.mNumVertices; ++v)
				{
					const aiVector3D t = mesh.HasTextureCoords(0) ? mesh.mTextureCoords[0][v] : aiVector3D();
					out.f32(t.x); out.f32(t.y);
				}
				for (const std::uint32_t i : indices)
					out.u32(i);

				report.vertices_ += mesh.mNumVertices;
				report.triangles_ += indices.size() / 3u;
			}

			report.meshes_ = scene->mNumMeshes;
			report.cookedBytes_ = out.finish();
		}
		catch (...)
		{
			std::error_code ignored;
			std::filesystem::remove(temporary, ignored);
			throw;
		}

		std::filesystem::rename(temporary, output);

		return report;
	}

	/*!
		@brief
			Deletes cooked .vmdl files whose source is gone, and this tool's stray .vmdl.tmp files.
		@return
			Paths removed, relative to the root
		*/
	std::vector<std::string> ModelCompiler::removeOrphans() const
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

			// .cooked/models/foo.fbx.vmdl.tmp is ours; other cookers' files are not.
			if (ext == L".tmp")
			{
				if (lowerExtension(entry.path().stem()) == kCookedExtension)
					stale.push_back(entry.path());
				continue;
			}

			if (ext != kCookedExtension)
				continue;

			// .cooked/models/foo.fbx.vmdl -> models/foo.fbx
			std::filesystem::path source = root_ / entry.path().lexically_relative(cookedRoot);
			source.replace_extension();

			if (!isSourceModel(source) || !std::filesystem::exists(source))
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
