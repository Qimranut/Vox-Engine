/*************************************************************************************************************
\file       FileWatcher.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares FileWatcher, which polls one file's modification time and size to notice when another
            program (a text editor saving a recipe) has changed it, reporting a change only once it settles.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// stds
#include <cstdint>
#include <filesystem>
#include <optional>


namespace vox::editor
{
	/**
	 * Notices that one file was changed by another program - a recipe saved from a text editor.
	 *
	 * Polls the modification time and size a couple of times a second, which costs nothing, needs
	 * no platform API and cannot miss an edit the way a change notification with a full buffer
	 * can. A change is reported once the file has looked the same on two polls in a row: editors
	 * write in pieces, and a recipe read half written is a parse error the user never made.
	 *
	 */
	class FileWatcher
	{
	public:

		/*!
			@brief
				Starts watching a file as it is now; an empty path watches nothing.

				The same path again changes nothing.
			@param file
				File to watch
			*/
		void watch(const std::filesystem::path& file);

		/*!
			@brief
				Reports a change to the watched file, at most one read of the disk per poll interval.
			@param now
				Seconds, monotonic
			@return
				True once for every change that has settled, including the file appearing or vanishing
			*/
		bool poll(double now);

		/*!
			@brief
				Takes the file as it is now for seen: the editor itself just wrote it.
			*/
		void acknowledge();

		/*!
			@brief
				Returns the watched file.
			@return
				The path being watched; empty when nothing is watched
			*/
		const std::filesystem::path&	file() const	{ return file_; }

	private:

		/**
		 * What one poll reads of the file; two equal stamps mean it has not changed.
		 *
		 */
		struct Stamp
		{
			bool							exists	{};
			std::filesystem::file_time_type	time	{};
			std::uintmax_t					size	{};

			bool operator==(const Stamp&) const = default;
		};

		/*!
			@brief
				Reads the file as it is on disk now.
			@return
				Its stamp; a default Stamp when it is missing or cannot be read
			*/
		Stamp read() const;

		std::filesystem::path	file_;
		Stamp					seen_;
		std::optional<Stamp>	settling_;
		double					nextPoll_	{};
	};
}
