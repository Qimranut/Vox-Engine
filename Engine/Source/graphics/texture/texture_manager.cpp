/*************************************************************************************************************
\file       texture_manager.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      TextureManager: generational texture slots, path and asset-key caches per colour space,
            the built-in white and missing textures, hot reload and collection of unused textures.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "texture_manager.hpp"

// Resources
#include "resources/ResourceManager.hpp"

// stds
#include <exception>
#include <iostream>
#include <stdexcept>


namespace graphics::texture
{
	namespace
	{
		/*!
			@brief
				Maps a colour space to its cache array index.
			@param colorSpace
				Colour space to map
			@return
				1 for Srgb, 0 for Linear
			*/
		std::size_t spaceIndex(ColorSpace colorSpace)
		{
			return colorSpace == ColorSpace::Srgb ? 1 : 0;
		}

		/*!
			@brief
				Maps a cache array index back to its colour space.
			@param index
				Cache array index
			@return
				Srgb for 1, Linear otherwise
			*/
		ColorSpace spaceAt(std::size_t index)
		{
			return index == 1 ? ColorSpace::Srgb : ColorSpace::Linear;
		}
	}

	/*!
		@brief
			Creates the built-in 1x1 white and 64x64 magenta checker textures. Needs a current context.
		@param resources
			Decodes the image files. Must outlive the manager.
		*/
	TextureManager::TextureManager(vox::ResourceManager& resources)
		:
		resources_{ resources }
	{
		const unsigned char whitePixel[4]{ 255, 255, 255, 255 };

		white_ = insert(Texture2D{ 1, 1, vox::PixelFormat::RGBA8, whitePixel, ColorSpace::Srgb,
			"builtin/white", false }, "builtin/white", ColorSpace::Srgb, true);

		// 64x64 in 8-texel cells: coarse enough to read at a distance, and a
		// colour no real material in the palette uses.
		constexpr int size = 64;
		constexpr int cell = 8;

		std::vector<unsigned char> checker(static_cast<std::size_t>(size) * size * 4);

		for (int y = 0; y < size; ++y)
		{
			for (int x = 0; x < size; ++x)
			{
				const bool magenta = ((x / cell) + (y / cell)) % 2 == 0;
				const std::size_t i = static_cast<std::size_t>(y * size + x) * 4;

				checker[i]		= magenta ? 255 : 0;
				checker[i + 1]	= 0;
				checker[i + 2]	= magenta ? 255 : 0;
				checker[i + 3]	= 255;
			}
		}

		missing_ = insert(Texture2D{ size, size, vox::PixelFormat::RGBA8, checker, ColorSpace::Srgb,
			"builtin/missing" }, "builtin/missing", ColorSpace::Srgb, true);
	}

	/*!
		@brief
			Finds or loads the texture for an asset path.

			A path already seen is one hash lookup on the string as written; a new
			spelling is normalised to an asset key and shared with other spellings.
			Failures are logged and cached as missing().
		@param path
			Relative to the asset root, as stored in scenes and components
		@param colorSpace
			Srgb for colour, Linear for data
		@return
			A live handle; missing() if the file cannot be loaded
		*/
	TextureHandle TextureManager::acquire(std::string_view path, ColorSpace colorSpace)
	{
		PathMap& byPath = byPath_[spaceIndex(colorSpace)];

		if (const auto it = byPath.find(path); it != byPath.end() && isAlive(it->second))
			return it->second;

		// Slow path, once per spelling: normalise, then share by asset key.
		std::string key;

		try
		{
			key = resources_.relative(resources_.resolve(std::string{ path }));
		}
		catch (const std::exception& e)
		{
			logFailure(path, e.what());
			byPath.insert_or_assign(std::string{ path }, missing_);
			return missing_;
		}

		PathMap& byKey = byKey_[spaceIndex(colorSpace)];

		if (const auto it = byKey.find(key); it != byKey.end() && isAlive(it->second))
		{
			byPath.insert_or_assign(std::string{ path }, it->second);
			return it->second;
		}

		TextureHandle handle = missing_;

		try
		{
			const vox::ResourceHandle image = resources_.load(key);

			if (image->kind != vox::ResourceKind::Texture)
				throw std::runtime_error("not an image file");

			handle = insert(Texture2D{ *image, colorSpace, key }, key, colorSpace, false);
		}
		catch (const std::exception& e)
		{
			logFailure(path, e.what());
		}

		byKey.insert_or_assign(key, handle);
		byPath.insert_or_assign(std::string{ path }, handle);

		return handle;
	}

	/*!
		@brief
			Takes ownership of a texture built in code, which has no asset path.
		@param texture
			Texture to take over
		@return
			Handle to it
		*/
	TextureHandle TextureManager::add(Texture2D texture)
	{
		const ColorSpace colorSpace = texture.colorSpace();
		return insert(std::move(texture), std::string{}, colorSpace, false);
	}

	/*!
		@brief
			Frees a texture now and bumps its slot's generation so every copy of the handle goes stale.

			Built-in textures and dead handles are ignored. Cache entries that
			pointed at the handle are removed.
		@param handle
			Texture to free
		*/
	void TextureManager::release(TextureHandle handle)
	{
		if (!isAlive(handle))
			return;

		Slot& slot = slots_[handle.index_];

		if (slot.builtin_)
			return;

		PathMap& byKey = byKey_[spaceIndex(slot.colorSpace_)];

		if (const auto it = byKey.find(slot.key_); it != byKey.end() && it->second == handle)
			byKey.erase(it);

		std::erase_if(byPath_[spaceIndex(slot.colorSpace_)],
			[handle](const auto& entry) { return entry.second == handle; });

		slot.texture_.reset();
		slot.key_.clear();

		// Bumping the generation is what makes every outstanding copy stale.
		if (++slot.generation_ == 0)
			slot.generation_ = 1;

		free_.push_back(handle.index_);
	}

	/*!
		@brief
			Re-reads an asset from disk and re-uploads every colour-space variant of it in place.

			Handles stay valid; a variant that previously failed gets a real slot
			and paths cached as missing resolve again. If the new file fails, the
			old texture stays.
		@param path
			Asset path to reload
		@return
			True if at least one texture was replaced
		*/
	bool TextureManager::reload(std::string_view path)
	{
		std::string key;
		vox::ResourceHandle image;

		try
		{
			key = resources_.relative(resources_.resolve(std::string{ path }));
			image = resources_.reload(key);

			if (image->kind != vox::ResourceKind::Texture)
				throw std::runtime_error("not an image file");
		}
		catch (const std::exception& e)
		{
			logFailure(path, e.what());
			return false;
		}

		bool replaced = false;

		for (std::size_t space = 0; space < kColorSpaces; ++space)
		{
			PathMap& byKey = byKey_[space];
			const auto it = byKey.find(key);

			if (it == byKey.end())
				continue;

			try
			{
				Texture2D texture{ *image, spaceAt(space), key };

				if (it->second == missing_ || !isAlive(it->second))
				{
					// It failed before: give it a real slot, and let every spelling
					// that was cached as missing resolve again.
					it->second = insert(std::move(texture), key, spaceAt(space), false);

					std::erase_if(byPath_[space],
						[this](const auto& entry) { return entry.second == missing_; });
				}
				else
				{
					slots_[it->second.index_].texture_ = std::move(texture);
				}

				replaced = true;
			}
			catch (const std::exception& e)
			{
				logFailure(path, e.what());
			}
		}

		return replaced;
	}

	/*!
		@brief
			Frees every asset texture not in the given set, then lets the resource manager drop unheld pixels.

			Built-ins and code-owned textures (no key) are never freed here.
		@param inUse
			Handles to keep
		@return
			Number of textures freed
		*/
	std::size_t TextureManager::collectUnused(std::span<const TextureHandle> inUse)
	{
		std::vector<bool> keep(slots_.size(), false);

		for (const TextureHandle& handle : inUse)
		{
			if (isAlive(handle))
				keep[handle.index_] = true;
		}

		std::size_t freed = 0;

		for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(slots_.size()); ++i)
		{
			const Slot& slot = slots_[i];

			// Code-owned textures (no key) belong to whoever added them.
			if (keep[i] || slot.builtin_ || !slot.texture_ || slot.key_.empty())
				continue;

			release(TextureHandle{ i, slot.generation_ });
			++freed;
		}

		resources_.collectUnused();

		return freed;
	}

	/*!
		@brief
			Resolves a handle to its texture.
		@param handle
			Texture handle
		@return
			The texture, or the missing texture if the handle is stale
		*/
	const Texture2D& TextureManager::get(TextureHandle handle) const
	{
		if (isAlive(handle))
			return *slots_[handle.index_].texture_;

		return *slots_[missing_.index_].texture_;
	}

	/*!
		@brief
			Checks a handle's index and generation against its slot.
		@param handle
			Texture handle
		@return
			Whether the handle still names the texture it was issued for
		*/
	bool TextureManager::isAlive(TextureHandle handle) const
	{
		return handle.isValid()
			&& handle.index_ < slots_.size()
			&& slots_[handle.index_].generation_ == handle.generation_
			&& slots_[handle.index_].texture_.has_value();
	}

	/*!
		@brief
			Counts the slots that hold a texture.
		@return
			Live textures, built-ins included
		*/
	std::size_t TextureManager::liveCount() const
	{
		std::size_t count = 0;

		for (const Slot& slot : slots_)
		{
			if (slot.texture_)
				++count;
		}

		return count;
	}

	/*!
		@brief
			Stores a texture in a free slot (or a new one) and returns a handle to it.
		@param texture
			Texture to store
		@param key
			Normalised asset key; empty for code-owned textures
		@param colorSpace
			Colour space the texture was requested in
		@param builtin
			Whether release() and collection must ignore it
		@return
			Handle carrying the slot index and its current generation
		*/
	TextureHandle TextureManager::insert(Texture2D&& texture, std::string key, ColorSpace colorSpace, bool builtin)
	{
		std::uint32_t index = 0;

		if (!free_.empty())
		{
			index = free_.back();
			free_.pop_back();
		}
		else
		{
			index = static_cast<std::uint32_t>(slots_.size());
			slots_.emplace_back();
		}

		Slot& slot = slots_[index];

		slot.texture_.emplace(std::move(texture));
		slot.key_			= std::move(key);
		slot.colorSpace_	= colorSpace;
		slot.builtin_		= builtin;

		return TextureHandle{ index, slot.generation_ };
	}

	/*!
		@brief
			Logs that a path fell back to the missing texture.
		@param path
			Path as the caller wrote it
		@param reason
			Why it failed
		*/
	void TextureManager::logFailure(std::string_view path, const char* reason) const
	{
		std::cerr << "[TextureManager] using the missing texture for '" << path << "': " << reason << "\n";
	}
}
