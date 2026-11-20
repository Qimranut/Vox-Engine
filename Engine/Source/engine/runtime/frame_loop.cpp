/*************************************************************************************************************
\file       frame_loop.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Implements engine::FrameLoop: the idle frame cap, input, the fixed-timestep accumulator,
            the voxel update, camera, per-window rendering and present, audio and the scheduler, each
            wrapped in a profile phase.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "frame_loop.hpp"
#include "scene/SceneManager.hpp"

// Voxel world
#include "../../voxel/sys/voxel_sys.hpp"

// Engine
#include <vox/engine.hpp>
#include "../systems/system_manager.hpp"

// Subsystems
#include "../../window/sys/win_sys.hpp"
#include "../../events/bus.hpp"
#include "../../input/sys/input_sys.hpp"
#include "../../graphics/sys/gph_sys.hpp"
#include "../../physics/sys/phy_sys.hpp"
#include "../../audio/sys/audio_sys.hpp"
#include "../../scheduler/scheduler.hpp"

// Events published across subsystems
#include "../../graphics/events/graphics_events.hpp"

// Profiling
#include "../../profile/frame_stats.hpp"
#include "../../profile/profiler_gpu.hpp"

// Windows, for the timer resolution
#include <windows.h>
#include <timeapi.h>

// stds
#include <algorithm>
#include <thread>

#pragma comment(lib, "winmm.lib")


namespace engine
{
	/*!
		@brief
			Builds the loop and starts its clock at the current GLFW time.
		@param systems
			Fully initialised subsystems; must outlive the loop
		@param layer
			Optional presentation layer; null always plays
		*/
	FrameLoop::FrameLoop(SystemManager& systems, ApplicationLayer* layer)
		: systems_{ systems }
		, layer_{ layer }
		, curTime_{ glfwGetTime() }
	{
	}

	/*!
		@brief
			Releases the 1 ms timer resolution if the loop asked for it.
		*/
	FrameLoop::~FrameLoop()
	{
		if (fineTimer_)
			timeEndPeriod(1);
	}

	/*!
		@brief
			Runs one frame: frame cap, input, fixed steps, voxel world, camera,
			render and present every window, audio, scheduler.
		@return
			False once a window has asked to close; nothing after input ran
		*/
	bool FrameLoop::tick()
	{
		window::System& win = systems_.windowSys();
		graphics::System& gph = systems_.graphicsSys();
		physics::System& phys = systems_.physicsSys();

		//// Frame cap
		// Waits in the OS event queue rather than in a sleep, so any input
		// ends the wait. An event that arrives during a long (idle) interval
		// still leaves kWakeInterval between frames, which is what makes the
		// short interval a real cap while the mouse is moving. The callbacks
		// that run in here only enqueue; input is resolved below as always.
		//
		// A Windows wait only ends on a scheduler tick, 15.6 ms by default,
		// which turns a 1/60 s cap into ~50 fps. The loop asks for 1 ms ticks
		// once it first caps, waits until kSpinWindow before the target, and
		// yields through the rest, which a 1 ms tick can still overshoot.
		if (const double interval = layer_ ? layer_->frameInterval() : 0.0; interval > 0.0)
		{
			VOX_PROFILE_PHASE(Idle);

			constexpr double kWakeInterval = 1.0 / 60.0;
			constexpr double kSpinWindow = 0.002;

			if (!fineTimer_)
				fineTimer_ = timeBeginPeriod(1) == TIMERR_NOERROR;

			const double deadline = curTime_ + interval;
			const double earliest = curTime_ + std::min(interval, kWakeInterval);

			double now = glfwGetTime();
			bool woken = false;

			while (now < (woken ? earliest : deadline))
			{
				const double remaining = (woken ? earliest : deadline) - now;

				if (remaining > kSpinWindow)
					glfwWaitEventsTimeout(remaining - kSpinWindow);
				else
					std::this_thread::yield();

				// GLFW truncates the timeout to whole milliseconds, so a plain
				// timeout can end up to 1 ms short; only an earlier end is an event.
				const double after = glfwGetTime();
				woken = woken || after < deadline - 2.0 * kSpinWindow;
				now = after;
			}
		}

		{
			VOX_PROFILE_PHASE(Input);

			{
				// Its own zone: the Windows modal drag/resize loop blocks
				// inside here, which would otherwise read as slow input.
				VOX_PROFILE_SCOPE_C("glfwPollEvents", profile::colour::kWait);
				glfwPollEvents();
			}

			// Immediately after the OS pump and before anything that runs on
			// this frame's input: the GLFW callbacks above only enqueue, so
			// this is where raw input becomes bus events. Every input
			// subscriber - camera intent, camera mode toggle, Lua callbacks -
			// runs inside this call.
			//
			// The layer's claim is last frame's UI state, which is as fresh as
			// it can be: the UI has not run yet this frame.
			if (layer_)
				systems_.inputSys().setSuppressed(layer_->capturesKeyboard(), layer_->capturesMouse());

			systems_.inputSys().update();
		}

		for (auto& w : win.windows())
		{
			if (glfwWindowShouldClose(w))
				return false;
		}

		double newTime = glfwGetTime();
		double frameTime = newTime - curTime_;

		if (frameTime > 0.25)
			frameTime = 0.25;

		curTime_ = newTime;

		accumulator_ += frameTime;

		// The Editor's Play/Pause gates simulation and audio. Without a
		// layer (Game) it always plays.
		const bool playing = !layer_ || layer_->isPlaying();
		syncAudioPlayback(playing);

		// Fixed timestep. Physics steps in here. While paused the
		// accumulator still drains, so resuming does not replay the
		// whole pause as a burst of steps.
		{
			VOX_PROFILE_PHASE(Simulation);

			while (accumulator_ >= dt_)
			{
				if (playing)
				{
					simulate(dt_);
					time_ += dt_;
					profile::frameStats().countSimStep();
				}

				accumulator_ -= dt_;
			}
		}

		// Per-frame update goes here

		//// Voxel world
		// Drains worker results, applies queued edits, feeds the renderer's
		// upload queue and adds terrain bodies in batches. GL-free.
		{
			VOX_PROFILE_PHASE(World);
			systems_.voxelSys().update();
		}

		// Scripts are not re-run per frame: they react to input through
		// Input.On* callbacks, which fire inside input update().

		//// Physics presentation
		// Blend between the last two fixed steps by how far into the next
		// one real time already is. Without this, a 100 Hz simulation
		// shown at 144 Hz visibly stutters. Publishes
		// CharacterPoseUpdated, which moves the orbit camera's target.
		{
			VOX_PROFILE_PHASE(Camera);

			phys.interpolate(static_cast<float>(accumulator_ / dt_));

			//// Graphics
			// Real elapsed time, not the fixed dt: graphics is per-frame
			// presentation plus the camera, not simulation. Driving the
			// camera from the fixed-timestep simulate() above would consume the
			// same mouse delta 0..N times per frame, and would scale the
			// gamepad stick by the wrong dt. Its camera intent was already
			// filled in by the input subscribers during input update().
            gph.setScene2D(systems_.scenes().is2D());
            // Menu/UI scenes in Game have a fixed camera. Editor still allows pan/zoom.
            if ((layer_ && layer_->wantsCameraInput()) || (!layer_ && !gph.scene2D())) gph.update(frameTime);

			// Published here rather than by graphics, which is meant to move
			// onto a worker thread; the bus is main-thread only.
			const graphics::camera::Camera& cam = gph.camera();

			systems_.bus().publish(graphics::events::CameraUpdated{
				cam.position(), cam.forward(), cam.yaw(), cam.pitch() });
		}

		// The scene is its own phase even when the layer draws it from inside
		// its UI: phases attribute time to the innermost one open.
		const auto renderScene = [&gph]
		{
			VOX_PROFILE_PHASE(Scene);
			gph.render();
            SceneView result;
            const auto& camera = gph.camera();
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row) {
                    result.view[column * 4 + row] = camera.view()[column][row];
                    result.projection[column * 4 + row] = camera.projection()[column][row];
                }
            result.orthographic = camera.projectionMode() == graphics::camera::Projection::Orthographic;
            return result;
		};

		for (auto* window : win.windows())
		{
			glfwMakeContextCurrent(window);

			// Query objects belong to a context, so only the first window's
			// rendering is timed for the frame statistics.
			const bool timed = window == win.windows().front();

			if (timed)
				gpuTimer_.begin();

			int width{}, height{};
			glfwGetFramebufferSize(window, &width, &height);
			glViewport(0, 0, width, height);
			glClearColor(0.06f, 0.06f, 0.06f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT);

			if (layer_)
			{
				VOX_PROFILE_PHASE(Interface);
				layer_->onFrame(renderScene);
			}
			else
			{
				renderScene();
			}

			if (timed)
				gpuTimer_.end();

			{
				// With vsync on, this is where the frame waits for the
				// display. A long Present is idle time, not a slow frame.
				VOX_PROFILE_PHASE(Present);
				glfwSwapBuffers(window);
			}

			if (timed)
			{
				// After the swap: reads back the GPU zones and the frame
				// timer's queries that finished, without stalling on the rest.
				VOX_PROFILE_GPU_COLLECT();

				float gpuMs{};
				if (gpuTimer_.poll(gpuMs))
					profile::frameStats().setGpuMs(gpuMs);
			}
		}

		//// Audio
		// Service FMOD on the main thread even while simulation is paused.
		// Read the toolbar again so a click takes effect in this frame.
		{
			VOX_PROFILE_PHASE(Audio);
			syncAudioPlayback(!layer_ || layer_->isPlaying());
			systems_.audioSys().update(frameTime);
		}

		//// Scheduler - no systems registered at the moment
		{
			VOX_PROFILE_PHASE(Scheduler);
			systems_.schedulerSys().runFrame(static_cast<float>(frameTime));
		}

		// The frame boundary, for both views of it. Keep these two together
		// and last: everything above belongs to the frame they close.
		profile::frameStats().endFrame();
		VOX_PROFILE_PLOT("Simulation steps", static_cast<std::int64_t>(profile::frameStats().sample(0).simSteps_));
		VOX_PROFILE_FRAME();

		return true;
	}

	/*!
		@brief
			Runs one fixed simulation step: enables character input only in the
			orbit camera, then steps physics.
		@param dt
			Fixed timestep in seconds
		*/
	void FrameLoop::simulate(double dt)
	{
		VOX_PROFILE_SCOPE_C("Fixed step", profile::colour::kPhysics);

		physics::System& phys = systems_.physicsSys();

		// The player only steers the character from the third-person camera;
		// in the debug fly camera the same Move action flies the camera.
		phys.setCharacterInputEnabled(systems_.graphicsSys().cameraMode() == graphics::CameraMode::Orbit);

		phys.step(static_cast<float>(dt));
	}

	/*!
		@brief
			Publishes audio::SimulationPause when the play state changed since the
			last call.
		@param playing
			Whether the simulation is playing this frame
		*/
	void FrameLoop::syncAudioPlayback(bool playing)
	{
		if (playing != audioPlaying_)
		{
			systems_.bus().publish(audio::SimulationPause{ !playing });
			audioPlaying_ = playing;
		}
	}
}
