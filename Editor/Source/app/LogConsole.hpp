/*************************************************************************************************************
\file       LogConsole.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares the Editor's Console panel history: timestamped Info / Warn / Error messages with
            per-level filters, and the newest message for the status bar.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// stds
#include <deque>
#include <string>


namespace vox::editor
{
	/** How serious a console message is; decides its colour and which filter hides it. */
	enum class LogLevel
	{
		Info,
		Warn,
		Error
	};

	/**
	 * The Console panel's history. Every editor action and failure lands here, so
	 * nothing is lost when the next message arrives.
	 *
	 */
	class LogConsole
	{
	public:

		/*!
			@brief
				Appends a message stamped with the local time.

				Drops the oldest message past the history's limit, and scrolls the panel to
				the new one on the next draw.
			@param level
				Severity of the message
			@param text
				The message
			*/
		void add(LogLevel level, std::string text);

		/*!
			@brief
				Removes every message from the history.
			*/
		void clear() { entries_.clear(); }

		/*!
			@brief
				Draws the panel body; call between ImGui::Begin and End.
			*/
		void draw();

		/*!
			@brief
				Tells whether the history holds no messages.
			@return
				True when there are no messages
			*/
		bool empty() const { return entries_.empty(); }

		/*!
			@brief
				The newest message's text, for the status bar. Only valid when !empty().
			@return
				Text of the last message
			*/
		const std::string&	lastText() const	{ return entries_.back().text; }

		/*!
			@brief
				The newest message's severity, for the status bar. Only valid when !empty().
			@return
				Level of the last message
			*/
		LogLevel			lastLevel() const	{ return entries_.back().level; }

	private:

		/** One line of the history. */
		struct Entry
		{
			std::string	time;
			LogLevel	level;
			std::string	text;
		};

		std::deque<Entry>	entries_;

		// Which levels the panel shows.
		bool	showInfo_		{ true };
		bool	showWarn_		{ true };
		bool	showError_		{ true };

		// A message arrived since the last draw.
		bool	scrollToEnd_	{};
	};
}
