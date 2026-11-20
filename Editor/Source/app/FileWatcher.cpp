/*************************************************************************************************************
\file       FileWatcher.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Implements FileWatcher: polls a file's modification time and size at a fixed interval and
            reports a change once two consecutive polls agree on the new state.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "FileWatcher.hpp"


namespace vox::editor
{
	namespace
	{
		// Seconds between two reads of the file's modification time and size.
		constexpr double kPollSeconds = 0.4;
	}

	/*!
		@brief
			Reads the watched file's existence, modification time and size from disk.
		@return
			Its stamp; a default Stamp when the path is empty, missing, not a regular file or unreadable
		*/
	FileWatcher::Stamp FileWatcher::read() const
	{
		Stamp stamp;
		std::error_code ec;

		if (file_.empty() || !std::filesystem::is_regular_file(file_, ec))
			return stamp;

		stamp.time = std::filesystem::last_write_time(file_, ec);
		if (ec)
			return Stamp{};

		stamp.size = std::filesystem::file_size(file_, ec);
		if (ec)
			return Stamp{};

		stamp.exists = true;

		return stamp;
	}

	/*!
		@brief
			Starts watching a file as it is now; the same path again changes nothing.
		@param file
			File to watch; empty watches nothing
		*/
	void FileWatcher::watch(const std::filesystem::path& file)
	{
		if (file == file_)
			return;

		file_ = file;
		acknowledge();
	}

	/*!
		@brief
			Records the file's current state as seen and drops any change still settling.
		*/
	void FileWatcher::acknowledge()
	{
		seen_ = read();
		settling_.reset();
	}

	/*!
		@brief
			Reads the file at most once per poll interval and reports a change once it has settled.
		@param now
			Seconds, monotonic
		@return
			True once for every change seen the same on two polls in a row
		*/
	bool FileWatcher::poll(double now)
	{
		if (file_.empty() || now < nextPoll_)
			return false;

		nextPoll_ = now + kPollSeconds;

		const Stamp current = read();
		if (current == seen_)
		{
			settling_.reset();
			return false;
		}

		// Different from what was seen: report it once it has stopped changing.
		if (!settling_ || !(*settling_ == current))
		{
			settling_ = current;
			return false;
		}

		seen_ = current;
		settling_.reset();

		return true;
	}
}
