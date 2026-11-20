/*************************************************************************************************************
\file       gpu_frame_timer.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares GpuFrameTimer, a ring of GL_TIME_ELAPSED queries that measures one frame's GPU time
            for FrameStats and reads results back without stalling.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// stds
#include <array>
#include <cstddef>


namespace profile
{
	/**
	 * GPU time of one frame's rendering, for FrameStats - the number the
	 * application shows about itself. Per-pass GPU timing is Tracy's job
	 * (profiler_gpu.hpp); this is one GL_TIME_ELAPSED query around everything
	 * drawn into the window, always compiled.
	 *
	 * Results are read back without stalling: a query is only fetched once the
	 * driver says it is ready, a few frames later. While every query in the
	 * ring is still in flight, that frame simply goes untimed.
	 *
	 * GL_TIME_ELAPSED queries cannot nest, so nothing between begin() and
	 * end() may open another. Tracy's GPU zones use GL_TIMESTAMP and are fine.
	 *
	 * GL thread only. Destroy it while the context is still current.
	 *
	 */
	class GpuFrameTimer
	{
	public:

		GpuFrameTimer() = default;

		/*!
			@brief
				Deletes the query objects, if they were created.
			*/
		~GpuFrameTimer();

		GpuFrameTimer(const GpuFrameTimer&)				= delete;
		GpuFrameTimer& operator=(const GpuFrameTimer&)	= delete;

		/*!
			@brief
				Starts timing a frame; creates the queries on first use and skips
				the frame when every query is still in flight.
			*/
		void begin();

		/*!
			@brief
				Stops timing the frame begun by begin(), if one was opened.
			*/
		void end();

		/*!
			@brief
				Fetches every result that has become available.
			@param ms
				Set to the newest finished measurement, in milliseconds
			@return
				True when ms was written
			*/
		bool poll(float& ms);

	private:

		static constexpr std::size_t kQueries = 8;

		std::array<unsigned int, kQueries>	queries_	{};
		std::size_t							write_		{ 0 };	// next query to issue
		std::size_t							read_		{ 0 };	// oldest query in flight
		std::size_t							inFlight_	{ 0 };
		bool								created_	{ false };
		bool								open_		{ false };
	};
}
