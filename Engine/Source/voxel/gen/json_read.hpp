/*************************************************************************************************************
\file       json_read.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Strict JSON reading shared by the recipe and biome parsers. Internal to voxel/gen: include
            it from .cpp files only, it pulls in the JSON library. Everything here throws
            std::runtime_error without a file prefix; the entry point that knows which file is being
            read adds it.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// JSON
#include <nlohmann/json.hpp>

// stds
#include <charconv>
#include <initializer_list>
#include <stdexcept>
#include <string>


namespace voxel::jsonread
{
	using nlohmann::json;

	/*!
		@brief
			Throws std::runtime_error with the given message.
		@param what
			The error message
		*/
	[[noreturn]] inline void fail(const std::string& what)
	{
		throw std::runtime_error(what);
	}

	/*!
		@brief
			Throws unless object is an object whose keys are all in allowed.

			Unknown keys are errors: a typo must not silently fall back to a default.
		@param object
			The JSON value to check
		@param allowed
			Every key the object may carry
		@param where
			Name of the object, for the error message
		*/
	inline void onlyKeys(const json& object, std::initializer_list<const char*> allowed, const std::string& where)
	{
		if (!object.is_object())
			fail(where + " must be an object");

		for (const auto& [key, value] : object.items())
		{
			bool ok = false;
			for (const char* a : allowed)
				if (key == a) { ok = true; break; }
			if (!ok)
				fail("unknown key \"" + key + "\" in " + where);
		}
	}

	/*!
		@brief
			Reads an optional number, throwing if the key holds anything else.
		@param object
			The JSON object to read
		@param key
			Key to look up
		@param fallback
			Value when the key is absent
		@param where
			Name of the object, for the error message
		@return
			The number converted to T, or fallback
		*/
	template <typename T>
	T number(const json& object, const char* key, T fallback, const std::string& where)
	{
		const auto it = object.find(key);
		if (it == object.end())
			return fallback;
		if (!it->is_number())
			fail(std::string("\"") + key + "\" in " + where + " must be a number");
		return it->get<T>();
	}

	/*!
		@brief
			Reads an optional boolean, throwing if the key holds anything else.
		@param object
			The JSON object to read
		@param key
			Key to look up
		@param fallback
			Value when the key is absent
		@param where
			Name of the object, for the error message
		@return
			The boolean, or fallback
		*/
	inline bool flag(const json& object, const char* key, bool fallback, const std::string& where)
	{
		const auto it = object.find(key);
		if (it == object.end())
			return fallback;
		if (!it->is_boolean())
			fail(std::string("\"") + key + "\" in " + where + " must be true or false");
		return it->get<bool>();
	}

	/*!
		@brief
			Reads an optional string, throwing if the key holds anything else.
		@param object
			The JSON object to read
		@param key
			Key to look up
		@param fallback
			Value when the key is absent
		@param where
			Name of the object, for the error message
		@return
			The string, or fallback
		*/
	inline std::string text(const json& object, const char* key, const std::string& fallback, const std::string& where)
	{
		const auto it = object.find(key);
		if (it == object.end())
			return fallback;
		if (!it->is_string())
			fail(std::string("\"") + key + "\" in " + where + " must be a string");
		return it->get<std::string>();
	}

	/*!
		@brief
			Throws unless v is greater than zero.
		@param v
			Value to check
		@param key
			Key it was read from, for the error message
		@param where
			Name of the object, for the error message
		*/
	inline void positive(double v, const char* key, const std::string& where)
	{
		if (!(v > 0.0))
			fail(std::string("\"") + key + "\" in " + where + " must be positive");
	}

	/*!
		@brief
			Throws unless v is a slope strictly between 0 and 89 degrees.
		@param v
			Angle in degrees
		@param key
			Key it was read from, for the error message
		@param where
			Name of the object, for the error message
		*/
	inline void slopeDeg(float v, const char* key, const std::string& where)
	{
		if (!(v > 0.f && v < 89.f))
			fail(std::string("\"") + key + "\" in " + where + " must be between 0 and 89 degrees");
	}

	/*!
		@brief
			Throws unless v lies in [0, 1].
		@param v
			Value to check
		@param key
			Key it was read from, for the error message
		@param where
			Name of the object, for the error message
		*/
	inline void unit(double v, const char* key, const std::string& where)
	{
		if (!(v >= 0.0 && v <= 1.0))
			fail(std::string("\"") + key + "\" in " + where + " must be 0..1");
	}

	/*!
		@brief
			A float as the double whose shortest decimal form is the float's own, for writing.

			0.1f goes out as 0.1 rather than 0.10000000149011612, and still reads
			back as exactly the same float.
		@param v
			The float to convert
		@return
			The tidied double
		*/
	inline double tidy(float v)
	{
		char buffer[64];
		const std::to_chars_result written = std::to_chars(buffer, buffer + sizeof(buffer), v);
		double out = static_cast<double>(v);
		if (written.ec == std::errc{})
			std::from_chars(buffer, written.ptr, out);
		return out;
	}
}
