/*************************************************************************************************************
\file       PipelinePreview.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Background prepass runs for the Generation Pipeline panel: starting, cancelling and
            handing finished stage captures to the main thread.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "PipelinePreview.hpp"

// Profile
#include "profile/profiler.hpp"

// Voxel
#include "voxel/gen/terrain_generator.hpp"

// stds
#include <algorithm>
#include <chrono>


namespace vox::editor
{
	namespace
	{
		// Worker threads of the preview's own job queue. Enough to keep the
		// parallel passes short; the droplet pass, which dominates, is one
		// thread anyway.
		constexpr unsigned kWorkers = 4;
	}

	/*!
		@brief
			Creates the preview with its own job queue; no prepass runs until request().
		*/
	PipelinePreview::PipelinePreview()
		:
		jobs_(kWorkers)
	{
	}

	/*!
		@brief
			Cancels any running prepass and joins its thread.
		*/
	PipelinePreview::~PipelinePreview()
	{
		stop();
	}

	/*!
		@brief
			Cancels the running prepass, if any, and waits for its thread.
		*/
	void PipelinePreview::stop()
	{
		cancel_.store(true);

		if (thread_.joinable())
			thread_.join();

		cancel_.store(false);
		running_.store(false);
	}

	/*!
		@brief
			Gives up a prepass nobody is looking at any more.

			The next request() for the same recipe starts it again; a finished result stays.
		*/
	void PipelinePreview::abandon()
	{
		if (!running_.load())
			return;

		stop();
		wanted_.reset();
	}

	/*!
		@brief
			How far the running prepass has got.
		@return
			0..1 of the running prepass.
		*/
	float PipelinePreview::progress() const
	{
		const int total = total_.load();

		return total > 0
			? std::clamp(static_cast<float>(done_.load()) / static_cast<float>(total), 0.f, 1.f)
			: 0.f;
	}

	/*!
		@brief
			Starts previewing a recipe, dropping a run that is still going.

			A recipe equal to the one being shown or built is ignored, so this can be called every
			frame.
		@param recipe
			Recipe whose prepass to run, copied.
		@param materials
			Material table the recipe's names resolve against, copied.
		*/
	void PipelinePreview::request(const voxel::Recipe& recipe, const voxel::MaterialTable& materials)
	{
		if (wanted_ && *wanted_ == recipe)
			return;

		stop();

		wanted_ = recipe;
		done_.store(0);
		total_.store(0);
		running_.store(true);

		thread_ = std::thread(
			[this, recipe, materials]
			{
				profile::nameThread("Pipeline preview");
				VOX_PROFILE_SCOPE_C("Pipeline preview", profile::colour::kEditor);

				auto result = std::make_unique<Result>();
				result->recipe = recipe;

				const auto started = std::chrono::steady_clock::now();

				try
				{
					const voxel::TerrainGenerator generator(recipe, materials);

					// Only the prepass ticks here; the rest of workUnits() is the voxel fill.
					total_.store(std::max(
						1,
						generator.workUnits() - generator.dims().sections_.x * generator.dims().sections_.z));

					const voxel::MaterialId water = materials.id(recipe.water_.material_);
					voxel::preview::CapturingObserver observer(
						water == voxel::kUnknownMaterial ? voxel::kAir : water);

					if (!generator.buildMaps(&jobs_, &done_, &cancel_, &observer))
					{
						// cancelled: a newer request is on its way
						running_.store(false);
						return;
					}

					result->stages = std::move(observer.stages());
				}
				catch (const std::exception& e)
				{
					result->error = e.what();
				}

				result->totalMs =
					std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
						.count();

				{
					std::lock_guard<std::mutex> lock(mutex_);
					finished_ = std::move(result);
				}

				running_.store(false);
			});
	}

	/*!
		@brief
			Takes over a finished run from the thread. Main thread, once a frame.
		@return
			Whether result() changed.
		*/
	bool PipelinePreview::poll()
	{
		std::lock_guard<std::mutex> lock(mutex_);

		if (!finished_)
			return false;

		shown_ = std::move(finished_);

		return true;
	}
}
