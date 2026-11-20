/*************************************************************************************************************
\file       PipelinePreview.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Runs a recipe's generation prepass on a background thread with its own job queue and keeps
            a capture of every stage, for the Editor's Generation Pipeline panel.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Engine
#include "util/thread/job_queue.hpp"

// Voxel
#include "voxel/gen/preview/stage_preview.hpp"
#include "voxel/gen/recipe.hpp"
#include "voxel/types/material_table.hpp"

// stds
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>


namespace vox::editor
{
	/**
	 * Runs a recipe's prepass in the background and keeps what every stage
	 * looked like: the data behind the Generation Pipeline panel, and what
	 * makes a recipe edit visible within a second or two without regenerating
	 * a single voxel.
	 *
	 * It has its own thread and its own small job queue. The voxel system's
	 * queue is not shared on purpose: that one going idle is what "the terrain
	 * is ready" and "the level can be saved" mean, and a preview must not hold
	 * either up.
	 *
	 */
	class PipelinePreview
	{
	public:

		/**
		 * One finished run of the prepass.
		 *
		 */
		struct Result
		{
			voxel::Recipe								recipe;
			std::vector<voxel::preview::StageCapture>	stages;		// empty when it failed
			std::string									error;
			double										totalMs		{};
		};

		/*!
			@brief
				Creates the preview with its own job queue; no prepass runs until request().
			*/
		PipelinePreview();

		/*!
			@brief
				Cancels any running prepass and joins its thread.
			*/
		~PipelinePreview();

		PipelinePreview(const PipelinePreview&) = delete;
		PipelinePreview& operator=(const PipelinePreview&) = delete;

		/*!
			@brief
				Starts previewing a recipe, dropping a run that is still going.

				A recipe equal to the one being shown or built is ignored, so this can be called
				every frame.
			@param recipe
				Recipe whose prepass to run, copied.
			@param materials
				Material table the recipe's names resolve against, copied.
			*/
		void request(const voxel::Recipe& recipe, const voxel::MaterialTable& materials);

		/*!
			@brief
				Takes over a finished run from the thread. Main thread, once a frame.
			@return
				Whether result() changed.
			*/
		bool poll();

		/*!
			@brief
				The last finished run.
			@return
				The run, or null before the first.
			*/
		const Result* result() const { return shown_.get(); }

		/*!
			@brief
				Whether a prepass is running.
			@return
				True while the preview thread works.
			*/
		bool busy() const { return running_.load(); }

		/*!
			@brief
				How far the running prepass has got.
			@return
				0..1 of the running prepass.
			*/
		float progress() const;

		/*!
			@brief
				Gives up a prepass nobody is looking at any more.

				The next request() for the same recipe starts it again; a finished result stays.
			*/
		void abandon();

	private:

		/*!
			@brief
				Cancels the running prepass, if any, and waits for its thread.
			*/
		void stop();

		util::thread::JobQueue			jobs_;
		std::thread						thread_;
		std::atomic<bool>				cancel_		{ false };
		std::atomic<bool>				running_	{ false };
		std::atomic<int>				done_		{ 0 };
		std::atomic<int>				total_		{ 0 };
		std::optional<voxel::Recipe>	wanted_;	// what the running or last run is for

		std::mutex				mutex_;
		std::unique_ptr<Result>	finished_;	// handed over by the thread
		std::unique_ptr<Result>	shown_;
	};
}
