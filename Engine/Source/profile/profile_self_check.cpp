/*************************************************************************************************************
\file       profile_self_check.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Implements the [profile-check] startup self-check: exclusive nested phase times, per-frame
            counters, history ring wrap-around and summary consistency, on a private FrameStats.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "profile_self_check.hpp"

#include "frame_stats.hpp"

// stds
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <thread>


namespace profile
{
	/*!
		@brief
			Runs the frame statistics checks on a private FrameStats and prints
			the result or one line per failure.
		*/
	void runSelfCheck()
	{
		int failures = 0;

		const auto check = [&failures](bool ok, const char* what)
		{
			if (!ok)
			{
				std::cerr << "[profile-check] FAILED: " << what << "\n";
				++failures;
			}
		};

		// Its own instance, so the check never shows up in the real history.
		// On the heap: the ring is too large for a stack frame.
		const auto stats = std::make_unique<FrameStats>();

		check(stats->size() == 0, "a new history is not empty");
		check(stats->summarise().frames_ == 0, "summary of an empty history is not empty");

		// ------------------------ Exclusive time ------------------------
		//
		// Scene runs inside Interface in the Editor. If the outer phase kept
		// the inner one's time, the breakdown would add up to more than the
		// frame and the UI would look as expensive as the scene it hosts.

		constexpr auto kInner = std::chrono::milliseconds(4);

		stats->beginPhase(Phase::Interface);
		stats->beginPhase(Phase::Scene);
		std::this_thread::sleep_for(kInner);
		stats->endPhase();
		stats->endPhase();
		stats->countSimStep();
		stats->countSimStep();
		stats->setGpuMs(1.5f);
		stats->endFrame();

		check(stats->size() == 1, "endFrame did not record a frame");

		{
			const FrameSample& f = stats->sample(0);
			const float scene = f.phaseMs_[static_cast<std::size_t>(Phase::Scene)];
			const float ui = f.phaseMs_[static_cast<std::size_t>(Phase::Interface)];

			// sleep_for never returns early, so the lower bound is exact.
			check(scene >= 4.f, "nested phase lost its time");
			check(ui < scene * 0.5f, "outer phase kept the nested phase's time - not exclusive");
			check(scene + ui <= f.frameMs_ + 0.01f, "phases add up to more than the frame");
			check(f.otherMs() >= 0.f, "unaccounted time is negative");
			check(f.simSteps_ == 2, "simulation steps were not counted");
			check(f.gpuMs_ == 1.5f, "GPU time was not carried into the frame");
		}

		// A frame starts clean: nothing leaks over from the one before.
		stats->endFrame();
		check(stats->sample(0).simSteps_ == 0 && stats->sample(0).phaseMs_[static_cast<std::size_t>(Phase::Scene)] == 0.f,
			"a frame inherited the previous frame's counters");

		// ----------------------------- Ring -----------------------------
		//
		// Marked by step count: frame i carries i steps, so after wrapping the
		// newest must be the last one pushed and the oldest exactly kHistory
		// behind it. An off-by-one here shows the panel a stale frame forever.

		const std::size_t total = FrameStats::kHistory + 7;

		for (std::size_t i = 0; i < total; ++i)
		{
			for (std::size_t s = 0; s < i % 5; ++s)
				stats->countSimStep();

			stats->endFrame();
		}

		check(stats->size() == FrameStats::kHistory, "history grew past its capacity");
		check(stats->sample(0).simSteps_ == (total - 1) % 5, "sample(0) is not the newest frame after wrapping");
		check(stats->sample(FrameStats::kHistory - 1).simSteps_ == (total - FrameStats::kHistory) % 5,
			"the oldest sample is wrong after wrapping");

		// ---------------------------- Summary ----------------------------

		const FrameSummary s = stats->summarise();

		check(s.frames_ == FrameStats::kHistory, "summary does not cover the whole history");
		check(s.minMs_ <= s.averageMs_ && s.averageMs_ <= s.maxMs_, "average is outside [min, max]");
		check(s.low1Ms_ >= s.averageMs_ && s.low1Ms_ <= s.maxMs_, "1% low is outside [average, max]");
		check(s.averageMs_ <= 0.f || (s.fps_ > 0.f && std::abs(s.fps_ * s.averageMs_ - 1000.f) < 1.f),
			"fps and average frame time disagree");

		if (failures == 0)
			std::cout << "[profile-check] all checks passed\n";
	}
}
