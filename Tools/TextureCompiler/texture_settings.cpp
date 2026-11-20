/*************************************************************************************************************
\file       texture_settings.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Resolves a source image's import settings: parses its .texmeta sidecar, falls back to the name
            suffix and extension, and rejects keys that do not apply to the resolved kind.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "texture_settings.hpp"

// stds
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>


namespace tools::texture
{
	namespace
	{
		/*!
			@brief
				Returns an ASCII lower-case copy of a string.
			@param text
				The text to convert
			@return
				The lower-case copy
			*/
		std::string lower(std::string_view text)
		{
			std::string out{ text };
			std::transform(out.begin(), out.end(), out.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}

		/*!
			@brief
				Strips spaces, tabs and carriage returns from both ends.
			@param text
				The text to trim
			@return
				A view of the trimmed text, empty if nothing remains
			*/
		std::string_view trim(std::string_view text)
		{
			const auto first = text.find_first_not_of(" \t\r");

			if (first == std::string_view::npos)
				return {};

			const auto last = text.find_last_not_of(" \t\r");
			return text.substr(first, last - first + 1);
		}

		/*!
			@brief
				Parses true/1/yes or false/0/no; throws std::runtime_error on anything else.
			@param value
				The lower-case value text
			@param where
				File and line, for the error message
			@return
				The parsed boolean
			*/
		bool parseBool(const std::string& value, const std::string& where)
		{
			if (value == "true" || value == "1" || value == "yes")
				return true;

			if (value == "false" || value == "0" || value == "no")
				return false;

			throw std::runtime_error(where + ": expected true or false, got '" + value + "'");
		}

		/*!
			@brief
				Infers the texture kind from the file name suffix.
			@param source
				The source image
			@return
				Normal for _n/_normal, Mask for _mask/_r/_m/_ao/_h/_sdf, otherwise nullopt
			*/
		std::optional<TextureKind> kindFromSuffix(const std::filesystem::path& source)
		{
			const std::string stem = lower(source.stem().string());

			const auto endsWith = [&stem](std::string_view suffix) { return stem.ends_with(suffix); };

			constexpr std::array<std::string_view, 2> normal{ "_n", "_normal" };
			constexpr std::array<std::string_view, 6> mask{ "_mask", "_r", "_m", "_ao", "_h", "_sdf" };

			if (std::any_of(normal.begin(), normal.end(), endsWith))
				return TextureKind::Normal;

			if (std::any_of(mask.begin(), mask.end(), endsWith))
				return TextureKind::Mask;

			return std::nullopt;
		}
	}

	/*!
		@brief
			Names a texture kind for logs and error messages.
		@param kind
			The texture kind
		@return
			"albedo", "normal", "mask" or "hdr"
		*/
	const char* kindName(TextureKind kind)
	{
		switch (kind)
		{
		case TextureKind::Normal:	return "normal";
		case TextureKind::Mask:		return "mask";
		case TextureKind::Hdr:		return "hdr";
		case TextureKind::Albedo:
		default:					return "albedo";
		}
	}

	/*!
		@brief
			Checks whether a file is an image the compiler cooks.
		@param path
			The file to inspect
		@return
			True for png, jpg, jpeg, bmp, tga and hdr (case-insensitive)
		*/
	bool isSourceImage(const std::filesystem::path& path)
	{
		const std::string ext = lower(path.extension().string());

		return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga" || ext == ".hdr";
	}

	/*!
		@brief
			Returns the settings sidecar path for a source image.
		@param source
			The source image
		@return
			<source>.texmeta
		*/
	std::filesystem::path sidecarPath(const std::filesystem::path& source)
	{
		std::filesystem::path meta = source;
		meta += ".texmeta";
		return meta;
	}

	/*!
		@brief
			Resolves a source image's settings from its suffix / extension, then its .texmeta sidecar.

			Sidecar keys that do not apply to the resolved kind are checked after the whole file is read,
			so key order does not matter. Throws std::runtime_error on an unreadable or malformed sidecar.
		@param source
			The source image
		@return
			The settings
		*/
	TextureSettings resolveSettings(const std::filesystem::path& source)
	{
		TextureSettings settings;

		if (const auto kind = kindFromSuffix(source))
			settings.kind_ = *kind;
		else if (lower(source.extension().string()) == ".hdr")
			settings.kind_ = TextureKind::Hdr;

		const std::filesystem::path meta = sidecarPath(source);

		if (!std::filesystem::exists(meta))
			return settings;

		std::ifstream in(meta);

		if (!in)
			throw std::runtime_error("cannot read " + meta.filename().string());

		std::optional<std::string> format;
		std::optional<bool> flipGreen;
		std::optional<float> alphaCutoff;

		std::string line;
		int lineNumber = 0;

		while (std::getline(in, line))
		{
			++lineNumber;

			std::string_view text = line;

			if (const auto hash = text.find('#'); hash != std::string_view::npos)
				text = text.substr(0, hash);

			text = trim(text);

			if (text.empty())
				continue;

			const std::string where = meta.filename().string() + ":" + std::to_string(lineNumber);
			const auto equals = text.find('=');

			if (equals == std::string_view::npos)
				throw std::runtime_error(where + ": expected 'key = value'");

			const std::string key = lower(trim(text.substr(0, equals)));
			const std::string value = lower(trim(text.substr(equals + 1)));

			if (key == "kind")
			{
				if (value == "albedo")		settings.kind_ = TextureKind::Albedo;
				else if (value == "normal")	settings.kind_ = TextureKind::Normal;
				else if (value == "mask")	settings.kind_ = TextureKind::Mask;
				else if (value == "hdr")	settings.kind_ = TextureKind::Hdr;
				else throw std::runtime_error(where + ": kind must be albedo, normal, mask or hdr");
			}
			else if (key == "format")
			{
				if (value != "bc7" && value != "bc1" && value != "bc3")
					throw std::runtime_error(where + ": format must be bc7, bc1 or bc3");
				format = value;
			}
			else if (key == "mips")
			{
				settings.mips_ = parseBool(value, where);
			}
			else if (key == "flip_green")
			{
				flipGreen = parseBool(value, where);
			}
			else if (key == "alpha_cutoff")
			{
				float cutoff = 0.f;
				const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), cutoff);

				if (error != std::errc{} || end != value.data() + value.size() || cutoff <= 0.f || cutoff > 1.f)
					throw std::runtime_error(where + ": alpha_cutoff must be a number in (0, 1]");

				alphaCutoff = cutoff;
			}
			else
			{
				throw std::runtime_error(where + ": unknown key '" + key + "'");
			}
		}

		// Checked after the whole file, so key order does not matter.
		const std::string name = meta.filename().string();

		if (format)
		{
			if (settings.kind_ != TextureKind::Albedo)
				throw std::runtime_error(name + ": format applies to albedo textures only; the "
					+ std::string{ kindName(settings.kind_) } + " format is fixed");

			settings.albedoFormat_ = *format == "bc1" ? AlbedoFormat::BC1
				: *format == "bc3" ? AlbedoFormat::BC3 : AlbedoFormat::BC7;
		}

		if (flipGreen)
		{
			if (settings.kind_ != TextureKind::Normal)
				throw std::runtime_error(name + ": flip_green applies to normal maps only");

			settings.flipGreen_ = *flipGreen;
		}

		if (alphaCutoff)
		{
			if (settings.kind_ != TextureKind::Albedo)
				throw std::runtime_error(name + ": alpha_cutoff applies to albedo textures only");

			settings.alphaCutoff_ = *alphaCutoff;
		}

		return settings;
	}
}
