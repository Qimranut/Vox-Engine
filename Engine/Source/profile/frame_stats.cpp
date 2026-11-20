/*************************************************************************************************************
\file       frame_stats.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Implements FrameStats: nested exclusive phase timing, the frame history ring, and the
            summaries (average, min/max, 1% low, per-phase) the Editor's Profiler panel shows.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "frame_stats.hpp"

// stds
#include <algorithm>
#include <cassert>
#include <vector>


namespace profile
{
	namespace
	{
		constexpr std::array<const char*, kPhaseCount> kLabels
		{
#define X(id, label, rgb) label,
			VOX_PROFILE_PHASE_LIST
#undef X
		};

		constexpr std::array<std::uint32_t, kPhaseCount> kColours
		{
#define X(id, label, rgb) rgb,
			VOX_PROFILE_PHASE_LIST
#undef X
		};

		/*!
			@brief
				Converts a clock duration to milliseconds.
			@param d
				Duration on the FrameStats clock
			@return
				The duration in milliseconds
			*/
		float toMs(FrameStats::Clock::duration d)
		{
			return std::chrono::duration<float, std::milli>(d).count();
		}
	}

	/*!
		@brief
			Display name of a phase.
		@param phase
			The phase
		@return
			The phase's display name
		*/
	const char* phaseLabel(Phase phase)
	{
		return kLabels[static_cast<std::size_t>(phase)];
	}

	/*!
		@brief
			Colour of a phase.
		@param phase
			The phase
		@return
			The phase's colour as 0xRRGGBB
		*/
	std::uint32_t phaseColour(Phase phase)
	{
		return kColours[static_cast<std::size_t>(phase)];
	}

	/*!
		@brief
			Frame time no phase accounts for.
		@return
			Frame time minus the sum of phase times, clamped at 0, in ms
		*/
	float FrameSample::otherMs() const
	{
		float accounted = 0.f;
		for (const float ms : phaseMs_)
			accounted += ms;

		return std::max(0.f, frameMs_ - accounted);
	}

	/*!
		@brief
			Opens a phase, pushing it on the nesting stack (dropped past kMaxDepth).
		@param phase
			The phase to open
		*/
	void FrameStats::beginPhase(Phase phase)
	{
		assert(depth_ < kMaxDepth && "phase nesting deeper than FrameStats::kMaxDepth");

		// Past the limit the phase is dropped, not the process: endPhase()
		// mirrors this check so the stack stays balanced.
		if (depth_ < kMaxDepth)
			stack_[depth_] = Open{ phase, Clock::now(), Clock::duration::zero() };

		++depth_;
	}

	/*!
		@brief
			Closes the innermost phase, adding its exclusive time to the frame and
			its total to the parent's children.
		*/
	void FrameStats::endPhase()
	{
		assert(depth_ > 0 && "endPhase without beginPhase");

		if (depth_ == 0)
			return;

		--depth_;

		if (depth_ >= kMaxDepth)
			return;

		const Open& open = stack_[depth_];
		const Clock::duration total = Clock::now() - open.start_;

		// Exclusive time: what the nested phases took is theirs, not ours.
		current_.phaseMs_[static_cast<std::size_t>(open.phase_)] += toMs(total - open.children_);

		if (depth_ > 0)
			stack_[depth_ - 1].children_ += total;
	}

	/*!
		@brief
			Closes the frame being built, stamps its frame and GPU time, and
			pushes it into the history ring.
		*/
	void FrameStats::endFrame()
	{
		assert(depth_ == 0 && "a phase is still open at the end of the frame");

		const Clock::time_point now = Clock::now();

		current_.frameMs_ = toMs(now - frameStart_);
		current_.gpuMs_ = gpuMs_;
		frameStart_ = now;

		history_[head_] = current_;
		head_ = (head_ + 1) % kHistory;
		count_ = std::min(count_ + 1, kHistory);
		++frameIndex_;

		current_ = FrameSample{};
	}

	/*!
		@brief
			One frame from the history ring.
		@param age
			0 is the newest finished frame, size() - 1 the oldest
		@return
			The frame sample
		*/
	const FrameSample& FrameStats::sample(std::size_t age) const
	{
		assert(age < count_);

		// head_ is the next write, so the newest frame sits one behind it.
		return history_[(head_ + kHistory - 1 - (age % kHistory)) % kHistory];
	}

	/*!
		@brief
			Copies the history out, oldest frame first.
		@param out
			Replaced with the history
		*/
	void FrameStats::copyHistory(std::vector<FrameSample>& out) const
	{
		out.clear();
		out.reserve(count_);

		for (std::size_t age = count_; age-- > 0;)
			out.push_back(sample(age));
	}

	/*!
		@brief
			Aggregates over the whole history.
		@return
			The summary of every frame in the history
		*/
	FrameSummary FrameStats::summarise() const
	{
		std::vector<FrameSample> frames;
		copyHistory(frames);

		return profile::summarise(frames);
	}

	/*!
		@brief
			Aggregates over a run of frames: averages, min/max, 1% low and per-phase times.
		@param frames
			Any run of frames
		@return
			Their aggregates; all zero for an empty run
		*/
	FrameSummary summarise(std::span<const FrameSample> frames)
	{
		const std::size_t count = frames.size();

		FrameSummary s;
		s.frames_ = count;

		if (count == 0)
			return s;

		std::vector<float> times;
		times.reserve(count);

		double total = 0.0, gpu = 0.0, other = 0.0;
		std::array<double, kPhaseCount> phases{};

		for (const FrameSample& f : frames)
		{
			times.push_back(f.frameMs_);
			total += f.frameMs_;
			gpu += f.gpuMs_;
			other += f.otherMs();

			for (std::size_t p = 0; p < kPhaseCount; ++p)
			{
				phases[p] += f.phaseMs_[p];
				s.phaseMaxMs_[p] = std::max(s.phaseMaxMs_[p], f.phaseMs_[p]);
			}
		}

		const double n = static_cast<double>(count);

		s.averageMs_ = static_cast<float>(total / n);
		s.fps_ = s.averageMs_ > 0.f ? 1000.f / s.averageMs_ : 0.f;
		s.gpuMs_ = static_cast<float>(gpu / n);
		s.otherAvgMs_ = static_cast<float>(other / n);

		for (std::size_t p = 0; p < kPhaseCount; ++p)
			s.phaseAvgMs_[p] = static_cast<float>(phases[p] / n);

		// "1% low": the mean of the slowest hundredth of the frames, which is
		// what a player feels as stutter and what an average hides.
		std::sort(times.begin(), times.end());

		s.minMs_ = times.front();
		s.maxMs_ = times.back();

		const std::size_t worst = std::max<std::size_t>(1, count / 100);
		double slow = 0.0;
		for (std::size_t i = count - worst; i < count; ++i)
			slow += times[i];

		s.low1Ms_ = static_cast<float>(slow / static_cast<double>(worst));

		return s;
	}

	/*!
		@brief
			The process-wide FrameStats instance.
		@return
			The function-local static instance
		*/
	FrameStats& frameStats()
	{
		static FrameStats instance;
		return instance;
	}
}
