/*************************************************************************************************************
\file       ProfilerPanel.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Implements the Profiler panel: headline figures, the stacked frame graph, the per-phase
            table, and finding and launching the Tracy viewer from Tools/Tracy.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "ProfilerPanel.hpp"

// Profile
#include "profile/profiler.hpp"

// ImGui
#include <imgui.h>

// Windows
#include <Windows.h>
#include <shellapi.h>

// stds
#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <iterator>


namespace vox::editor
{
	namespace
	{
		// A frame time the user can hold the graph and the verdict colours against.
		struct Budget
		{
			const char*	label;
			float		ms;
		};

		// What "fast enough" means is the user's call: the ship target is 60, and the higher ones are
		// for checking headroom on a fast monitor.
		constexpr Budget kBudgets[] = {
			{ "60 fps  (16.7 ms)", 1000.0f / 60.0f },
			{ "120 fps  (8.3 ms)", 1000.0f / 120.0f },
			{ "144 fps  (6.9 ms)", 1000.0f / 144.0f },
			{ "30 fps  (33.3 ms)", 1000.0f / 30.0f },
		};

		// The viewer's name in Tracy's Windows release zip, then what people rename it to.
		constexpr const wchar_t* kViewerNames[] = { L"tracy-profiler.exe", L"Tracy.exe", L"tracy.exe" };

		// seconds; summarise() sorts, and steady numbers read better
		constexpr double kSummaryInterval = 0.25;

		// Height of the frame graph.
		constexpr float kGraphHeight = 120.0f;

		// Verdict colours: within the budget, up to half as much again, worse.
		const ImVec4 kGood(0.45f, 0.80f, 0.50f, 1);
		const ImVec4 kWarn(1.00f, 0.80f, 0.35f, 1);
		const ImVec4 kBad(1.00f, 0.45f, 0.40f, 1);

		/*!
			@brief
				Converts 0xRRGGBB to an opaque ImGui colour.
			@param rgb
				Colour as 0xRRGGBB
			@return
				The packed ImGui colour with full alpha
			*/
		ImU32 opaque(std::uint32_t rgb)
		{
			return IM_COL32((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff, 255);
		}

		/*!
			@brief
				Picks the colour a time earns against a budget.
			@param ms
				The time in milliseconds
			@param budget
				The budget in milliseconds
			@return
				Good within the budget, warn up to 1.5 times it, bad beyond
			*/
		const ImVec4& verdict(float ms, float budget)
		{
			return ms <= budget ? kGood
				: ms <= budget * 1.5f ? kWarn
				: kBad;
		}

		// Colour of "Other", the frame time no phase accounts for.
		constexpr std::uint32_t kOtherColour = 0x3A3A3A;
	}

	/*!
		@brief
			Sets where the Tracy viewer is looked for, and forgets the one found before.
		@param repoRoot
			Folder holding Tools/; the viewer is looked for in Tools/Tracy under it
		*/
	void ProfilerPanel::setRepoRoot(const std::filesystem::path& repoRoot)
	{
		toolsFolder_ = repoRoot / "Tools" / "Tracy";
		viewer_.clear();
		viewerCheckedAt_ = -1;
	}

	/*!
		@brief
			The selected budget in milliseconds.
		@return
			Frame time the graph and verdict colours are held against
		*/
	float ProfilerPanel::budgetMs() const
	{
		return kBudgets[budget_].ms;
	}

	/*!
		@brief
			The live numbers for the status bar, re-summarised at most every kSummaryInterval seconds.

			Independent of Freeze and of whether the panel is open.
		@return
			The latest live frame summary
		*/
	const profile::FrameSummary& ProfilerPanel::liveSummary()
	{
		const double now = ImGui::GetTime();

		if (liveAt_ < 0 || now - liveAt_ >= kSummaryInterval)
		{
			live_ = profile::frameStats().summarise();
			liveAt_ = now;
		}

		return live_;
	}

	/*!
		@brief
			Copies the live history and, a few times a second, re-summarises it; nothing while frozen.
		*/
	void ProfilerPanel::refresh()
	{
		if (frozen_)
			return;

		profile::frameStats().copyHistory(frames_);

		const double now = ImGui::GetTime();

		if (summaryAt_ < 0 || now - summaryAt_ >= kSummaryInterval)
		{
			summary_ = profile::summarise(frames_);
			summaryAt_ = now;
		}
	}

	/*!
		@brief
			Draws the panel body: refreshes, then headline, controls, graph, table and Tracy section.

			Call between ImGui::Begin and End.
		@param actions
			Where the panel sends its messages
		*/
	void ProfilerPanel::draw(const Actions& actions)
	{
		VOX_PROFILE_SCOPE_C("Profiler panel", profile::colour::kEditor);

		refresh();

		headline();
		controls();
		graph();
		breakdown();
		tracySection(actions);
	}

	/*!
		@brief
			Draws the FPS, frame, 1% low, worst and GPU figures, coloured against the budget.
		*/
	void ProfilerPanel::headline() const
	{
		const float budget = budgetMs();

		const auto stat = [](const char* label, const ImVec4& colour, const char* format, float value)
		{
			ImGui::BeginGroup();
			ImGui::TextDisabled("%s", label);
			ImGui::TextColored(colour, format, value);
			ImGui::EndGroup();
			ImGui::SameLine(0, 28);
		};

		const ImVec4 plain = ImGui::GetStyleColorVec4(ImGuiCol_Text);

		stat("FPS", verdict(summary_.averageMs_, budget), "%.0f", summary_.fps_);
		stat("Frame", verdict(summary_.averageMs_, budget), "%.2f ms", summary_.averageMs_);
		stat("1% low", verdict(summary_.low1Ms_, budget), "%.2f ms", summary_.low1Ms_);
		stat("Worst", verdict(summary_.maxMs_, budget), "%.2f ms", summary_.maxMs_);
		stat("GPU", plain, "%.2f ms", summary_.gpuMs_);
		ImGui::NewLine();
	}

	/*!
		@brief
			Draws the Freeze and By phase checkboxes and the budget combo; unfreezing forces a
			fresh summary.
		*/
	void ProfilerPanel::controls()
	{
		if (ImGui::Checkbox("Freeze", &frozen_) && !frozen_)
			summaryAt_ = -1;
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
			ImGui::SetTooltip(
				"Hold the last %zu frames still to inspect a hitch.\nThe engine keeps recording underneath.",
				profile::FrameStats::kHistory);

		ImGui::SameLine(0, 16);
		ImGui::Checkbox("By phase", &stacked_);
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
			ImGui::SetTooltip("Stack each frame by where the main thread spent it.\nOff: colour each frame "
							  "by the budget instead.");

		ImGui::SameLine(0, 16);
		ImGui::SetNextItemWidth(170);
		if (ImGui::BeginCombo("Budget", kBudgets[budget_].label))
		{
			for (int i = 0; i < static_cast<int>(std::size(kBudgets)); ++i)
				if (ImGui::Selectable(kBudgets[i].label, i == budget_))
					budget_ = i;

			ImGui::EndCombo();
		}
	}

	/*!
		@brief
			Draws the scrolling frame graph on a fixed scale of twice the budget, stacked by phase
			or coloured by budget, with the budget line, red caps on clipped spikes and a tooltip
			for the hovered frame.
		*/
	void ProfilerPanel::graph()
	{
		// --------------------------- Frame ----------------------------

		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const float width = std::max(64.0f, ImGui::GetContentRegionAvail().x);
		const ImVec2 size(width, kGraphHeight);

		ImGui::InvisibleButton("##frames", size);
		const bool hovered = ImGui::IsItemHovered();

		ImDrawList* draw = ImGui::GetWindowDrawList();
		draw->AddRectFilled(
			origin,
			ImVec2(origin.x + size.x, origin.y + size.y),
			ImGui::GetColorU32(ImGuiCol_FrameBg));

		// --------------------------- Scale ----------------------------

		// A fixed scale of twice the budget, so the budget line sits mid-height and the graph does
		// not rescale under the eye whenever one frame spikes. Taller frames are clipped and capped.
		const float budget = budgetMs(), scale = budget * 2.0f;
		const float bottom = origin.y + size.y;

		// Slots are sized for a full history, so frames enter at the right edge and scroll left.
		const float slot = size.x / static_cast<float>(profile::FrameStats::kHistory);
		const float left = origin.x + size.x - slot * static_cast<float>(frames_.size());

		const auto heightOf = [&](float ms)
		{
			return std::min(ms, scale) / scale * size.y;
		};

		int hoveredIndex = -1;
		if (hovered && !frames_.empty())
		{
			const int index = static_cast<int>((ImGui::GetIO().MousePos.x - left) / slot);
			if (index >= 0 && index < static_cast<int>(frames_.size()))
				hoveredIndex = index;
		}

		// --------------------------- Bars -----------------------------

		draw->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);

		for (std::size_t i = 0; i < frames_.size(); ++i)
		{
			const profile::FrameSample& frame = frames_[i];
			const float x0 = left + slot * static_cast<float>(i), x1 = std::max(x0 + 1.0f, x0 + slot);

			if (stacked_)
			{
				// Bottom-up in phase order, clipped at the top of the graph; "Other" last.
				float used = 0;
				const auto segment = [&](float ms, ImU32 colour)
				{
					if (ms <= 0 || used >= scale)
						return;

					const float y1 = bottom - heightOf(used), y0 = bottom - heightOf(used + ms);
					if (y1 - y0 >= 0.5f)
						draw->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), colour);

					used += ms;
				};

				for (std::size_t p = 0; p < profile::kPhaseCount; ++p)
					segment(frame.phaseMs_[p], opaque(profile::phaseColour(static_cast<profile::Phase>(p))));
				segment(frame.otherMs(), opaque(kOtherColour));
			}
			else
				draw->AddRectFilled(
					ImVec2(x0, bottom - heightOf(frame.frameMs_)),
					ImVec2(x1, bottom),
					ImGui::GetColorU32(verdict(frame.frameMs_, budget)));

			// Off the scale: a red cap, so a clipped spike never looks like an ordinary tall frame.
			if (frame.frameMs_ > scale)
				draw->AddRectFilled(ImVec2(x0, origin.y), ImVec2(x1, origin.y + 3), ImGui::GetColorU32(kBad));

			if (static_cast<int>(i) == hoveredIndex)
				draw->AddRectFilled(ImVec2(x0, origin.y), ImVec2(x1, bottom), IM_COL32(255, 255, 255, 48));
		}

		// ------------------------ Budget line -------------------------

		const float budgetY = bottom - heightOf(budget);
		draw->AddLine(
			ImVec2(origin.x, budgetY),
			ImVec2(origin.x + size.x, budgetY),
			IM_COL32(255, 255, 255, 110));

		char label[32];
		std::snprintf(label, sizeof(label), "%.1f ms", budget);
		draw->AddText(
			ImVec2(origin.x + 4, budgetY - ImGui::GetTextLineHeight() - 1),
			IM_COL32(255, 255, 255, 150),
			label);

		std::snprintf(label, sizeof(label), "%.1f ms", scale);
		draw->AddText(ImVec2(origin.x + 4, origin.y + 2), IM_COL32(255, 255, 255, 90), label);

		draw->PopClipRect();

		if (hoveredIndex >= 0)
			frameTooltip(frames_[static_cast<std::size_t>(hoveredIndex)]);
	}

	/*!
		@brief
			Draws the tooltip for one frame: its time, fps, GPU time, sim steps and per-phase times.
		@param frame
			The hovered frame
		*/
	void ProfilerPanel::frameTooltip(const profile::FrameSample& frame) const
	{
		if (!ImGui::BeginTooltip())
			return;

		ImGui::TextColored(verdict(frame.frameMs_, budgetMs()), "%.2f ms", frame.frameMs_);
		ImGui::SameLine();
		ImGui::TextDisabled(
			"(%.0f fps)   GPU %.2f ms   %u sim step%s",
			frame.frameMs_ > 0 ? 1000.0f / frame.frameMs_ : 0.0f,
			frame.gpuMs_,
			frame.simSteps_,
			frame.simSteps_ == 1 ? "" : "s");
		ImGui::Separator();

		const auto row = [](std::uint32_t rgb, const char* name, float ms)
		{
			if (ms < 0.005f)
				return;

			ImGui::ColorButton(
				name,
				ImGui::ColorConvertU32ToFloat4(opaque(rgb)),
				ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder,
				ImVec2(10, 10));
			ImGui::SameLine();
			ImGui::Text("%-22s %6.2f ms", name, ms);
		};

		for (std::size_t p = 0; p < profile::kPhaseCount; ++p)
		{
			const auto phase = static_cast<profile::Phase>(p);
			row(profile::phaseColour(phase), profile::phaseLabel(phase), frame.phaseMs_[p]);
		}
		row(kOtherColour, "Other", frame.otherMs());

		ImGui::EndTooltip();
	}

	/*!
		@brief
			Draws the table of average and worst time per main-thread phase, plus Other, and each
			one's share of the average frame.
		*/
	void ProfilerPanel::breakdown() const
	{
		ImGui::Spacing();

		const ImGuiTableFlags flags =
			ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
		if (!ImGui::BeginTable("##phases", 4, flags))
			return;

		ImGui::TableSetupColumn("Main thread", ImGuiTableColumnFlags_WidthFixed, 190);
		ImGui::TableSetupColumn("Average", ImGuiTableColumnFlags_WidthFixed, 80);
		ImGui::TableSetupColumn("Worst", ImGuiTableColumnFlags_WidthFixed, 80);
		ImGui::TableSetupColumn("Share of frame", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableHeadersRow();

		const float frame = std::max(summary_.averageMs_, 0.0001f);

		const auto row = [&](std::uint32_t rgb, const char* name, float average, float worst, const char* tip)
		{
			ImGui::TableNextRow();

			ImGui::TableNextColumn();
			ImGui::ColorButton(
				name,
				ImGui::ColorConvertU32ToFloat4(opaque(rgb)),
				ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder,
				ImVec2(10, 10));
			ImGui::SameLine();
			ImGui::TextUnformatted(name);
			if (tip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
				ImGui::SetTooltip("%s", tip);

			ImGui::TableNextColumn();
			ImGui::Text("%.2f ms", average);

			ImGui::TableNextColumn();
			if (worst >= 0)
				ImGui::Text("%.2f ms", worst);
			else
				ImGui::TextDisabled("-");

			ImGui::TableNextColumn();
			char overlay[16];
			std::snprintf(overlay, sizeof(overlay), "%.0f%%", average / frame * 100.0f);
			ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImGui::ColorConvertU32ToFloat4(opaque(rgb)));
			ImGui::ProgressBar(std::clamp(average / frame, 0.0f, 1.0f), ImVec2(-FLT_MIN, 0), overlay);
			ImGui::PopStyleColor();
		};

		for (std::size_t p = 0; p < profile::kPhaseCount; ++p)
		{
			const auto phase = static_cast<profile::Phase>(p);
			const char* tip =
				phase == profile::Phase::Present
					? "Waiting for the display when vsync is on.\nIdle time, not work: a long "
					  "Present means there is headroom."
				: phase == profile::Phase::Interface
					? "The editor's own panels, without the scene drawn inside the Viewport."
				: phase == profile::Phase::Idle
					? "Waiting out the frame cap (Window > Performance). Idle time, not work."
				: nullptr;

			row(profile::phaseColour(phase),
				profile::phaseLabel(phase),
				summary_.phaseAvgMs_[p],
				summary_.phaseMaxMs_[p],
				tip);
		}
		row(kOtherColour, "Other", summary_.otherAvgMs_, -1, "Frame time no phase accounts for.");

		ImGui::EndTable();

		ImGui::TextDisabled(
			"Times are exclusive: a phase does not include the phases inside it. Over the last %zu frames.",
			summary_.frames_);
	}

	/*!
		@brief
			Draws the Tracy section: whether the build is instrumented, the client's state and
			build info, and buttons to launch the viewer or copy its address.
		@param actions
			Where launch results are reported
		*/
	void ProfilerPanel::tracySection(const Actions& actions)
	{
		ImGui::Spacing();
		ImGui::SeparatorText("Tracy");

		const profile::BuildInfo build = profile::buildInfo();
		if (!build.enabled_)
		{
			ImGui::TextWrapped("This build has no Tracy instrumentation. Configure with -DVOX_PROFILER=ON "
							   "(the default) and rebuild to capture "
							   "every zone, thread, GPU pass and lock in the Tracy viewer.");
			return;
		}

		const bool connected = profile::viewerConnected();
		ImGui::TextColored(
			connected ? kGood : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),
			connected ? "Viewer connected - recording" : "Idle - no viewer attached");
		if (!connected && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
			ImGui::SetTooltip(
				"The client records nothing until a viewer connects, so leaving it compiled in is free.");

		ImGui::SameLine(0, 16);
		ImGui::TextDisabled(
			"client %s   %s:%u%s",
			build.clientVersion_,
			build.remote_ ? "any interface" : "localhost",
			build.port_,
			build.memory_ ? "   memory tracking" : "");

		findViewer();

		ImGui::BeginDisabled(viewer_.empty() || connected);
		if (ImGui::Button("Open Tracy and connect"))
			launchViewer(actions);
		ImGui::EndDisabled();

		ImGui::SameLine();
		if (ImGui::Button("Copy address"))
		{
			char address[32];
			std::snprintf(address, sizeof(address), "127.0.0.1:%u", build.port_);
			ImGui::SetClipboardText(address);
		}

		if (viewer_.empty())
			ImGui::TextWrapped(
				"Viewer not found. Download windows-%s.zip from the Tracy releases page "
				"(github.com/wolfpld/tracy/releases), "
				"and unzip it into %s. The version must match the client exactly.",
				build.clientVersion_,
				toolsFolder_.string().c_str());
	}

	/*!
		@brief
			Looks for the Tracy viewer under each known name in toolsFolder_, at most once a second.
		*/
	void ProfilerPanel::findViewer()
	{
		const double now = ImGui::GetTime();

		if (viewerCheckedAt_ < 0 || now - viewerCheckedAt_ >= 1.0)
		{
			viewerCheckedAt_ = now;
			viewer_.clear();

			std::error_code ec;
			for (const wchar_t* name : kViewerNames)
				if (std::filesystem::is_regular_file(toolsFolder_ / name, ec))
				{
					viewer_ = toolsFolder_ / name;
					break;
				}
		}
	}

	/*!
		@brief
			Starts the Tracy viewer with -a 127.0.0.1 and reports the outcome through actions.
		@param actions
			Where success or failure is reported
		*/
	void ProfilerPanel::launchViewer(const Actions& actions)
	{
		// The viewer attaches to this process; it has to be the client's version, which is
		// why it is looked for in the repo's own Tools/Tracy and never on PATH.
		const std::wstring folder = viewer_.parent_path().native();
		const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(
			nullptr,
			L"open",
			viewer_.c_str(),
			L"-a 127.0.0.1",
			folder.c_str(),
			SW_SHOWNORMAL));

		if (result <= 32)
		{
			if (actions.error)
				actions.error(
					"Could not start " + viewer_.string() + " (ShellExecute code "
					+ std::to_string(result) + ").");
		}
		else if (actions.info)
			actions.info("Started the Tracy viewer; it connects to this editor on localhost.");
	}
}
