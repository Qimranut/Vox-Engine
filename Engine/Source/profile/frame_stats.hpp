/*************************************************************************************************************
\file       frame_stats.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      The numbers the application shows about ITSELF: frame time, where the main thread spent it,
            GPU time. Always compiled, with or without Tracy - this is what the Editor's Profiler panel
            and status bar read. Tracy is the tool for finding out WHY a frame was slow; it records
            every zone on every thread, but streams them to the viewer and hands nothing back to the
            process. This is the opposite trade: a handful of coarse, main-thread phases, kept in a
            ring, cheap enough to leave on in every build (two clock reads per phase per frame).
            VOX_PROFILE_PHASE opens both a Tracy zone and a FrameStats phase, so the two views always
            agree on what "Simulation" means. Main thread only: nothing here is synchronised, and
            FrameLoop is the only writer.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

#include "profiler.hpp"

// stds
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>


// X(Id, "label", 0xRRGGBB)
// The order is the order of the panel's breakdown and of its stacked colours.
// Phases may nest: time is attributed to the innermost open phase, so the
// columns of one frame never add up to more than the frame.
#define VOX_PROFILE_PHASE_LIST \
	X(Input,		"Input",			profile::colour::kInput)	\
	X(Simulation,	"Simulation",		profile::colour::kPhysics)	\
	X(World,		"Voxel world",		profile::colour::kVoxel)	\
	X(Camera,		"Interpolate + camera",	0x57B8A5)				\
	X(Scene,		"Scene render",		profile::colour::kGraphics)	\
	X(Interface,	"Editor UI",		profile::colour::kEditor)	\
	X(Present,		"Present (swap)",	profile::colour::kWait)		\
	X(Audio,		"Audio",			profile::colour::kAudio)	\
	X(Scheduler,	"Scheduler",		0xB0704F)					\
	X(Idle,			"Idle (frame cap)",	0x4A4F57)


namespace profile
{
	enum class Phase : std::uint8_t
	{
#define X(id, label, rgb) id,
		VOX_PROFILE_PHASE_LIST
#undef X
		Count
	};

	inline constexpr std::size_t kPhaseCount = static_cast<std::size_t>(Phase::Count);

	/*!
		@brief
			Display name of a phase.
		@param phase
			The phase
		@return
			The phase's display name
		*/
	const char* phaseLabel(Phase phase);

	/*!
		@brief
			Colour of a phase.
		@param phase
			The phase
		@return
			The phase's colour as 0xRRGGBB, the same one its Tracy zone uses
		*/
	std::uint32_t phaseColour(Phase phase);

	/** One finished frame. Times are milliseconds. */
	struct FrameSample
	{
		float							frameMs_	{ 0.f };	// end of the previous frame to the end of this one
		std::array<float, kPhaseCount>	phaseMs_	{};			// exclusive time per phase
		float							gpuMs_		{ 0.f };	// latest GPU frame time known when this frame ended
		std::uint32_t					simSteps_	{ 0 };		// fixed steps simulated in this frame

		/*!
			@brief
				Frame time no phase accounts for.
			@return
				Waiting between phases, the OS, the loop itself, in ms (never negative)
			*/
		float otherMs() const;
	};

	/** Aggregates over the frames currently in the history. */
	struct FrameSummary
	{
		std::size_t						frames_		{ 0 };
		float							fps_		{ 0.f };	// from the average frame time
		float							averageMs_	{ 0.f };
		float							minMs_		{ 0.f };
		float							maxMs_		{ 0.f };
		float							low1Ms_		{ 0.f };	// average of the slowest 1% of frames
		float							gpuMs_		{ 0.f };	// average
		std::array<float, kPhaseCount>	phaseAvgMs_	{};
		std::array<float, kPhaseCount>	phaseMaxMs_	{};
		float							otherAvgMs_	{ 0.f };
	};

	class FrameStats
	{
	public:

		// Eight seconds at 60 Hz: long enough to see a hitch scroll by, short
		// enough that the 1% low reacts when a scene changes.
		static constexpr std::size_t kHistory = 480;

		// Phases nest two deep today (Scene inside Interface).
		static constexpr std::size_t kMaxDepth = 8;

		using Clock = std::chrono::steady_clock;

		/*!
			@brief
				Opens a phase.

				Pair with endPhase(); use VOX_PROFILE_PHASE instead of calling this.
			@param phase
				The phase to open
			*/
		void beginPhase(Phase phase);

		/*!
			@brief
				Closes the innermost open phase and adds its exclusive time to the frame.
			*/
		void endPhase();

		/*!
			@brief
				Counts one fixed simulation step towards the frame being built.
			*/
		void countSimStep() { ++current_.simSteps_; }

		/*!
			@brief
				Sets the latest GPU frame time that has come back from the driver;
				it trails by a few frames.
			@param ms
				GPU frame time in milliseconds
			*/
		void setGpuMs(float ms) { gpuMs_ = ms; }

		/*!
			@brief
				Closes the frame being built and pushes it into the history.

				Exactly one call site: FrameLoop::tick, beside VOX_PROFILE_FRAME.
			*/
		void endFrame();

		/*!
			@brief
				Frames currently held, at most kHistory.
			@return
				The number of frames in the history
			*/
		std::size_t size() const { return count_; }

		/*!
			@brief
				One frame from the history.
			@param age
				0 is the newest finished frame, size() - 1 the oldest
			@return
				The frame sample
			*/
		const FrameSample& sample(std::size_t age) const;

		/*!
			@brief
				Copies the history out, oldest frame first - the order a graph
				draws it in.

				A reader that wants to freeze the view keeps the copy.
			@param out
				Replaced; reuse the same vector to avoid reallocating every frame
			*/
		void copyHistory(std::vector<FrameSample>& out) const;

		/*!
			@brief
				Aggregates over the whole history.

				Sorts a copy of the frame times, so call it a few times a second,
				not once per widget.
			@return
				The summary of every frame in the history
			*/
		FrameSummary summarise() const;

		/*!
			@brief
				Total frames ever finished; lets a reader tell whether anything is new.
			@return
				The finished frame count
			*/
		std::uint64_t frameIndex() const { return frameIndex_; }

	private:

		struct Open
		{
			Phase				phase_;
			Clock::time_point	start_;
			Clock::duration		children_;
		};

		std::array<FrameSample, kHistory>	history_	{};
		std::size_t							head_		{ 0 };	// next slot to write
		std::size_t							count_		{ 0 };
		std::uint64_t						frameIndex_	{ 0 };

		FrameSample							current_	{};
		std::array<Open, kMaxDepth>			stack_		{};
		std::size_t							depth_		{ 0 };
		float								gpuMs_		{ 0.f };

		Clock::time_point					frameStart_	{ Clock::now() };
	};

	/*!
		@brief
			Aggregates over any run of frames.
		@param frames
			Any run of frames, e.g. a frozen copy from FrameStats::copyHistory
		@return
			Their aggregates; all zero for an empty run
		*/
	FrameSummary summarise(std::span<const FrameSample> frames);

	/*!
		@brief
			The process-wide instance.

			A global on purpose: it is read by the Editor, which only receives
			what ApplicationLayer::onAttach hands over, and widening that public
			interface for a diagnostic is the worse trade.
		@return
			The process-wide FrameStats
		*/
	FrameStats& frameStats();

	/** RAII for beginPhase/endPhase. */
	class ScopedPhase
	{
	public:

		/*!
			@brief
				Opens the phase on the global FrameStats.
			@param phase
				The phase to open
			*/
		explicit ScopedPhase(Phase phase)	{ frameStats().beginPhase(phase); }

		/*!
			@brief
				Closes the phase opened by the constructor.
			*/
		~ScopedPhase()						{ frameStats().endPhase(); }

		ScopedPhase(const ScopedPhase&)				= delete;
		ScopedPhase& operator=(const ScopedPhase&)	= delete;
	};
}


namespace profile::detail
{
	// Tracy's zone macros build a constexpr source location, so the name has
	// to be a constant expression at the call site - these, not phaseLabel().
#define X(id, label, rgb) \
	inline constexpr const char*	kPhaseLabel_##id	= label; \
	inline constexpr std::uint32_t	kPhaseColour_##id	= rgb;
	VOX_PROFILE_PHASE_LIST
#undef X
}

// Opens a frame phase for the rest of the scope: a FrameStats phase always, and
// a Tracy zone of the same name and colour when the profiler is compiled in.
// `id` is a bare Phase enumerator: VOX_PROFILE_PHASE(Simulation);
// The zone is declared first so it closes last and encloses the phase.
#define VOX_PROFILE_PHASE(id) \
	VOX_PROFILE_SCOPE_C(profile::detail::kPhaseLabel_##id, profile::detail::kPhaseColour_##id); \
	const profile::ScopedPhase voxProfilePhase_##id { profile::Phase::id }
