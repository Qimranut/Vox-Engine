/*************************************************************************************************************
\file       frame_loop.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares engine::FrameLoop, which runs one engine frame at a time in a fixed order: frame cap,
            input, fixed-step simulation, voxel world, camera, rendering and present, audio, scheduler.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Profiling
#include "../../profile/gpu_frame_timer.hpp"


namespace engine
{
	class SystemManager;
	class ApplicationLayer;

	/**
	 * Runs one engine frame at a time, in a fixed order.
	 *
	 * 1. OS events, then input - every input subscriber runs here
	 * 2. Fixed-timestep simulation (physics), skipped while paused
	 * 3. Physics interpolation and debug lines
	 * 4. Camera update, then CameraUpdated
	 * 5. Render each window through the layer, then present
	 * 6. Audio, then the scheduler
	 *
	 * Each step is a VOX_PROFILE_PHASE, so the Editor's Profiler panel and a
	 * Tracy capture break the frame down the same way. A step added here
	 * needs a phase too (profile/frame_stats.hpp), or its time lands in
	 * "Other".
	 *
	 * Main thread only.
	 *
	 */
	class FrameLoop
	{
	public:

		/*!
			@brief
				Builds the loop and starts its clock.
			@param systems
				Fully initialised subsystems; must outlive the loop
			@param layer
				Optional presentation layer; null always plays
			*/
		FrameLoop(SystemManager& systems, ApplicationLayer* layer);

		/*!
			@brief
				Releases the 1 ms timer resolution if the loop asked for it.
			*/
		~FrameLoop();

		FrameLoop(const FrameLoop&) = delete;
		FrameLoop& operator=(const FrameLoop&) = delete;

		/*!
			@brief
				Runs one frame.
			@return
				False once a window has asked to close; nothing after input ran
			*/
		bool tick();

	private:

		/*!
			@brief
				Runs one fixed simulation step: enables character input only in the
				orbit camera, then steps physics.
			@param dt
				Fixed timestep in seconds
			*/
		void simulate(double dt);

		/*!
			@brief
				Publishes audio::SimulationPause when the play state changed since
				the last call.
			@param playing
				Whether the simulation is playing this frame
			*/
		void syncAudioPlayback(bool playing);

		SystemManager&		systems_;
		ApplicationLayer*	layer_;

		double	time_			{};
		double	dt_				{ 0.01 };
		double	curTime_		{};
		double	accumulator_	{};

		// Audio initializes paused.
		bool	audioPlaying_	{ false };

		// timeBeginPeriod(1) is in force; asked for on the first capped frame.
		bool	fineTimer_		{ false };

		// GPU time of the window's rendering, fed to profile::frameStats().
		// Destroyed with the loop, which VoxEngine::shutdown releases before
		// the subsystems, so the GL context is still alive.
		profile::GpuFrameTimer	gpuTimer_;
	};
}
