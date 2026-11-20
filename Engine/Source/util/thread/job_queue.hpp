/*************************************************************************************************************
\file       job_queue.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      JobQueue (a fixed worker pool over one FIFO of jobs) and MainThreadQueue (callbacks posted by
            workers and run on the main thread). Layout changes with the profiler; see profiler.hpp.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Profiling
#include "../../profile/profiler.hpp"

// stds
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>


namespace util::thread
{
	/**
	 * A fixed pool of worker threads draining one FIFO of jobs.
	 *
	 * Jobs must not touch OpenGL, the ECS registry, or events::Bus (which
	 * asserts its owner thread). They compute and hand results back through
	 * a MainThreadQueue. The destructor drains nothing: it stops the workers
	 * as soon as they finish their current job and joins them.
	 *
	 */
	class JobQueue
	{
	public:

		/*!
			@brief
				Starts the worker threads.
			@param workers
				Thread count; 0 picks hardware_concurrency - 1, at least 1
			*/
		explicit JobQueue(unsigned workers = 0);

		/*!
			@brief
				Stops the workers as soon as they finish their current job and joins them.

				Drains nothing: queued jobs are dropped.
			*/
		~JobQueue();

		JobQueue(const JobQueue&) = delete;
		JobQueue& operator=(const JobQueue&) = delete;

		/*!
			@brief
				Queues a job. Safe from any thread.
			@param job
				Work to run on a worker thread
			*/
		void push(std::function<void()> job);

		/*!
			@brief
				Jobs queued or running.
			@return
				Number of jobs not yet finished
			*/
		std::size_t pending() const;

		/*!
			@brief
				Blocks until every queued job has run.

				Main-thread convenience for shutdown and self-checks; never call it
				from a job.
			*/
		void waitIdle();

		/*!
			@brief
				Number of worker threads.
			@return
				The worker count
			*/
		unsigned workerCount() const { return static_cast<unsigned>(threads_.size()); }

	private:

		/*!
			@brief
				Worker body: takes and runs jobs until the queue stops.
			@param index
				Worker number, used in the thread name
			*/
		void threadLoop(unsigned index);

		// With the profiler compiled in this is a wrapper that reports who
		// held the lock and who waited for it; otherwise plain std::mutex.
		// The condition variables are the _any kind because the plain one
		// only accepts std::mutex.
		using Mutex = VOX_PROFILE_LOCKABLE_TYPE(std::mutex);

		std::vector<std::thread>			threads_;
		std::deque<std::function<void()>>	jobs_;
		mutable VOX_PROFILE_LOCKABLE(std::mutex, mutex_, "JobQueue");
		std::condition_variable_any			wake_;
		std::condition_variable_any			idle_;
		std::size_t							running_	{ 0 };
		bool								stop_		{ false };
	};

	/**
	 * Callbacks posted from worker threads and run on the main thread in
	 * drain(). This is how job results reach the ECS, GL and the bus.
	 *
	 */
	class MainThreadQueue
	{
	public:

		/*!
			@brief
				Queues a callback for the next drain(). Safe from any thread.
			@param fn
				Callback to run on the main thread
			*/
		void post(std::function<void()> fn);

		/*!
			@brief
				Runs every callback posted so far, in order.

				Callbacks may post new ones; those run on the next drain().
			@return
				Number of callbacks run
			*/
		std::size_t drain();

		/*!
			@brief
				Discards every posted callback without running it.
			*/
		void clear();

	private:

		std::deque<std::function<void()>>	queue_;
		std::mutex							mutex_;
	};
}
