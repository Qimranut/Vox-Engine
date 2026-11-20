/*************************************************************************************************************
\file       texture_manager.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      TextureManager, which owns every GPU texture and hands out generational handles keyed by
            (asset path, colour space), resolving failures and stale handles to a magenta missing texture.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Texture
#include "texture.hpp"
#include "texture_types.hpp"

// stds
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>


namespace vox
{
	class ResourceManager;
}

namespace graphics::texture
{
	/**
	 * Owns every GPU texture and hands out generational handles to them.
	 *
	 * Asset textures are keyed by (asset path, colour space). Pixels are
	 * decoded by vox::ResourceManager and uploaded here; the manager never
	 * keeps the CPU copy alive itself.
	 *
	 * Nothing here throws. A path that cannot be loaded is logged once and
	 * resolves to missing(), a magenta checkerboard that is impossible to
	 * mistake for real art. GL thread only.
	 *
	 */
	class TextureManager
	{
	public:

		/*!
			@brief
				Creates the built-in white and missing textures. Needs a current
				context.
			@param resources
				Decodes the image files. Must outlive the manager.
			*/
		explicit TextureManager(vox::ResourceManager& resources);

		/*!
			@brief
				Finds or loads the texture for an asset path.

				Cheap enough to call per draw: a path already seen is a single hash
				lookup on the string as written, with no filesystem access.
			@param path
				Relative to the asset root, as stored in scenes and components
			@param colorSpace
				Srgb for colour (albedo, emissive, UI), Linear for data
			@return
				A live handle; missing() if the file cannot be loaded
			*/
		TextureHandle acquire(std::string_view path, ColorSpace colorSpace);

		/*!
			@brief
				Takes ownership of a texture built in code - a generated palette, a
				render target - that has no asset path.
			@param texture
				Texture to take over
			@return
				Handle to it
			*/
		TextureHandle add(Texture2D texture);

		/*!
			@brief
				Frees a texture now.

				The handle, and every copy of it, becomes stale and resolves to
				missing(). Built-in textures ignore this.
			@param handle
				Texture to free
			*/
		void release(TextureHandle handle);

		/*!
			@brief
				Re-reads an asset from disk and re-uploads every colour-space variant
				of it in place.

				Handles stay valid; if the new file fails to load, the old texture
				stays.
			@param path
				Asset path to reload
			@return
				True if at least one texture was replaced
			*/
		bool reload(std::string_view path);

		/*!
			@brief
				Frees every asset texture not in the given set, then lets the
				resource manager drop pixels nobody holds.
			@param inUse
				Handles to keep
			@return
				Number of textures freed
			*/
		std::size_t collectUnused(std::span<const TextureHandle> inUse);

		/*!
			@brief
				Resolves a handle to its texture.
			@param handle
				Texture handle
			@return
				The texture, or the missing texture if the handle is stale. The
				reference is invalidated by the next acquire() or add().
			*/
		const Texture2D& get(TextureHandle handle) const;

		/*!
			@brief
				Checks a handle's generation against its slot.
			@param handle
				Texture handle
			@return
				Whether the handle still names the texture it was issued for
			*/
		bool isAlive(TextureHandle handle) const;

		/*!
			@brief
				1x1 opaque white.

				Multiplying by it is a no-op, so untextured draws use the same
				shader path as textured ones.
			@return
				Handle to the built-in white texture
			*/
		TextureHandle white() const { return white_; }

		/*!
			@brief
				Magenta and black checkerboard, for anything that failed to load.
			@return
				Handle to the built-in missing texture
			*/
		TextureHandle missing() const { return missing_; }

		/*!
			@brief
				Counts the textures currently held.
			@return
				Live textures, built-ins included
			*/
		std::size_t liveCount() const;

	private:

		struct Slot
		{
			std::optional<Texture2D>	texture_;
			std::string					key_;
			ColorSpace					colorSpace_		{ ColorSpace::Linear };
			std::uint32_t				generation_		{ 1 };
			bool						builtin_		{ false };
		};

		// Heterogeneous lookup: find() takes a string_view without allocating.
		struct StringHash
		{
			using is_transparent = void;

			/*!
				@brief
					Hashes a string view, so std::string keys and views hash alike.
				@param s
					String to hash
				@return
					The hash value
				*/
			std::size_t operator()(std::string_view s) const noexcept
			{
				return std::hash<std::string_view>{}(s);
			}
		};

		using PathMap = std::unordered_map<std::string, TextureHandle, StringHash, std::equal_to<>>;

		static constexpr std::size_t kColorSpaces = 2;

		/*!
			@brief
				Stores a texture in a free or new slot and returns a handle to it.
			@param texture
				Texture to store
			@param key
				Normalised asset key; empty for code-owned textures
			@param colorSpace
				Colour space the texture was requested in
			@param builtin
				Whether release() and collection must ignore it
			@return
				Handle to the stored texture
			*/
		TextureHandle insert(Texture2D&& texture, std::string key, ColorSpace colorSpace, bool builtin);

		/*!
			@brief
				Logs that a path fell back to the missing texture.
			@param path
				Path as the caller wrote it
			@param reason
				Why it failed
			*/
		void logFailure(std::string_view path, const char* reason) const;

		vox::ResourceManager&				resources_;

		std::vector<Slot>					slots_;
		std::vector<std::uint32_t>			free_;

		// Path exactly as the caller wrote it -> handle. The fast path.
		std::array<PathMap, kColorSpaces>	byPath_;

		// Normalised asset key -> handle, so different spellings of one file
		// share a texture. Also caches failures as missing().
		std::array<PathMap, kColorSpaces>	byKey_;

		TextureHandle						white_;
		TextureHandle						missing_;
	};
}
