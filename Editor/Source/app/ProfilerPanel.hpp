/*************************************************************************************************************
\file       ProfilerPanel.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares the Editor's Profiler panel: the frame graph and phase table read from
            profile::FrameStats, the status bar's live numbers, and the launcher for the Tracy viewer.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Profile
#include "profile/frame_stats.hpp"

// stds
#include <filesystem>
#include <functional>
#include <string>
#include <vector>


namespace vox::editor
{
	/**
	 * The Profiler panel: what the editor shows about its own frame, and the
	 * door to Tracy.
	 *
	 * Two tools, on purpose. The graph and the table here read
	 * profile::FrameStats - a few coarse main-thread phases, always on, good
	 * for "is it slow and roughly where". Tracy answers "why": every zone on
	 * every thread, the GPU, locks, memory. It is a separate viewer that
	 * attaches over localhost, so this panel only reports its state and
	 * starts it.
	 *
	 */
	class ProfilerPanel
	{
	public:

		/**
		 * Where the panel sends its messages. Either may be empty.
		 *
		 */
		struct Actions
		{
			std::function<void(const std::string&)>	info;
			std::function<void(const std::string&)>	error;
		};

		/*!
			@brief
				Sets where the Tracy viewer is looked for, and forgets the one found before.
			@param repoRoot
				Folder holding Tools/; the viewer is looked for in Tools/Tracy under it
			*/
		void setRepoRoot(const std::filesystem::path& repoRoot);

		/*!
			@brief
				Draws the panel body; call between ImGui::Begin and End.
			@param actions
				Where the panel sends its messages
			*/
		void draw(const Actions& actions);

		/*!
			@brief
				The live numbers for the status bar.

				Recomputed a few times a second whether or not the panel is open. Never the frozen view.
			@return
				The latest live frame summary
			*/
		const profile::FrameSummary& liveSummary();

	private:

		/*!
			@brief
				Takes the live history and, a few times a second, its summary; nothing while frozen.
			*/
		void refresh();

		// The panel's sections, in the order draw() runs them.

		/*!
			@brief
				Draws the FPS, frame, 1% low, worst and GPU figures, coloured against the budget.
			*/
		void headline() const;

		/*!
			@brief
				Draws the Freeze and By phase checkboxes and the budget combo.
			*/
		void controls();

		/*!
			@brief
				Draws the scrolling frame graph, stacked by phase or coloured by budget, with the
				budget line and a tooltip for the hovered frame.
			*/
		void graph();

		/*!
			@brief
				Draws the table of average and worst time per main-thread phase and its share of the frame.
			*/
		void breakdown() const;

		/*!
			@brief
				Draws the Tracy section: client state, build info, and buttons to launch the viewer
				or copy its address.
			@param actions
				Where launch results are reported
			*/
		void tracySection(const Actions& actions);

		/*!
			@brief
				Finds the Tracy viewer in toolsFolder_, at most once a second.
			*/
		void findViewer();

		/*!
			@brief
				Starts the Tracy viewer and reports the outcome through actions.
			@param actions
				Where success or failure is reported
			*/
		void launchViewer(const Actions& actions);

		/*!
			@brief
				Draws the tooltip for one frame: its time, fps, GPU time, sim steps and per-phase times.
			@param frame
				The hovered frame
			*/
		void	frameTooltip(const profile::FrameSample& frame) const;

		/*!
			@brief
				The selected budget in milliseconds.
			@return
				Frame time the graph and verdict colours are held against
			*/
		float	budgetMs() const;

		// What the panel draws: the live history, or the copy taken when Freeze was ticked.
		std::vector<profile::FrameSample>	frames_;
		profile::FrameSummary				summary_;
		double								summaryAt_	{ -1 };
		bool								frozen_		{};

		// Status bar numbers, on their own clock.
		profile::FrameSummary	live_;
		double					liveAt_		{ -1 };

		int		budget_		{ 0 };
		bool	stacked_	{ true };

		// Tracy viewer, resolved once per second so a freshly unzipped viewer is picked up.
		std::filesystem::path	toolsFolder_;
		std::filesystem::path	viewer_;
		double					viewerCheckedAt_	{ -1 };
	};
}
