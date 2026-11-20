/*************************************************************************************************************
\file       LogConsole.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Implements the Editor's Console panel: appending timestamped messages to a bounded history
            and drawing them with level filters and colours.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "LogConsole.hpp"

// ImGui
#include <imgui.h>

// stds
#include <cstdio>
#include <ctime>
#include <utility>


namespace vox::editor
{
	namespace
	{
		// Oldest messages are dropped past this many.
		constexpr size_t kMaxEntries = 500;

		/*!
			@brief
				Picks the text colour for a message of the given severity.
			@param level
				Severity of the message
			@return
				Red for errors, amber for warnings, green for info
			*/
		ImVec4 colourOf(LogLevel level)
		{
			if (level == LogLevel::Error)
				return ImVec4(1.00f, 0.42f, 0.38f, 1);

			if (level == LogLevel::Warn)
				return ImVec4(1.00f, 0.80f, 0.35f, 1);

			return ImVec4(0.62f, 0.88f, 0.62f, 1);
		}
	}

	/*!
		@brief
			Appends a message stamped with the local time (HH:MM:SS).

			Drops the oldest message past kMaxEntries and asks the next draw to scroll to the end.
		@param level
			Severity of the message
		@param text
			The message
		*/
	void LogConsole::add(LogLevel level, std::string text)
	{
		const std::time_t now = std::time(nullptr);
		std::tm local{};
		localtime_s(&local, &now);

		char stamp[16];
		std::snprintf(stamp, sizeof(stamp), "%02d:%02d:%02d", local.tm_hour, local.tm_min, local.tm_sec);

		entries_.push_back({ stamp, level, std::move(text) });
		if (entries_.size() > kMaxEntries)
			entries_.pop_front();

		scrollToEnd_ = true;
	}

	/*!
		@brief
			Draws the Clear button, the level filters and the filtered, coloured message list.

			Call between ImGui::Begin and End.
		*/
	void LogConsole::draw()
	{
		// Clear button and the level filters.
		if (ImGui::Button("Clear log"))
			clear();

		ImGui::SameLine();
		ImGui::Checkbox("Info", &showInfo_);
		ImGui::SameLine();
		ImGui::Checkbox("Warnings", &showWarn_);
		ImGui::SameLine();
		ImGui::Checkbox("Errors", &showError_);
		ImGui::Separator();

		ImGui::BeginChild(
			"LogEntries",
			ImVec2(0, 0),
			ImGuiChildFlags_None,
			ImGuiWindowFlags_HorizontalScrollbar);

		for (const auto& e : entries_)
		{
			if (e.level == LogLevel::Info && !showInfo_)
				continue;

			if (e.level == LogLevel::Warn && !showWarn_)
				continue;

			if (e.level == LogLevel::Error && !showError_)
				continue;

			ImGui::TextDisabled("%s", e.time.c_str());
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_Text, colourOf(e.level));
			ImGui::TextUnformatted(e.text.c_str());
			ImGui::PopStyleColor();
		}

		if (scrollToEnd_)
		{
			ImGui::SetScrollHereY(1.0f);
			scrollToEnd_ = false;
		}

		ImGui::EndChild();
	}
}
