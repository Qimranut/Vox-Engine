/*************************************************************************************************************
\file       job_queue.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      JobQueue, a fixed pool of worker threads draining one FIFO of jobs, and MainThreadQueue,
            which carries callbacks from workers back to the main thread.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "job_queue.hpp"

// Profiling
#include "../../profile/profiler.hpp"

// stds
#include <algorithm>
#include <utility>


namespace util::thread
{
	/*!
		@brief
			Starts the worker threads.
		@param workers
			Thread count; 0 picks hardware_concurrency - 1, at least 1
		*/
	JobQueue::JobQueue(unsigned workers)
	{
		if (workers == 0)
		{
			const unsigned hw = std::thread::hardware_concurrency();
			workers = hw > 1 ? hw - 1 : 1;
		}

		threads_.reserve(workers);
		for (unsigned i = 0; i < workers; ++i)
			threads_.emplace_back(&JobQueue::threadLoop, this, i);
	}

	/*!
		@brief
			Drops queued jobs, stops the workers once their current job ends and joins them.
		*/
	JobQueue::~JobQueue()
	{
		{
			std::lock_guard<Mutex> lock(mutex_);
			stop_ = true;
			jobs_.clear();
		}
		wake_.notify_all();

		for (std::thread& t : threads_)
		{
			if (t.joinable())
				t.join();
		}
	}

	/*!
		@brief
			Queues a job and wakes one worker; ignored once the queue is stopping.
		@param job
			Work to run on a worker thread
		*/
	void JobQueue::push(std::function<void()> job)
	{
		{
			std::lock_guard<Mutex> lock(mutex_);
			if (stop_)
				return;
			jobs_.push_back(std::move(job));
		}
		wake_.notify_one();
	}

	/*!
		@brief
			Counts jobs queued plus jobs running.
		@return
			Number of jobs not yet finished
		*/
	std::size_t JobQueue::pending() const
	{
		std::lock_guard<Mutex> lock(mutex_);
		return jobs_.size() + running_;
	}

	/*!
		@brief
			Blocks until the queue is empty and no job is running.
		*/
	void JobQueue::waitIdle()
	{
		std::unique_lock<Mutex> lock(mutex_);
		idle_.wait(lock, [this] { return jobs_.empty() && running_ == 0; });
	}

	/*!
		@brief
			Worker body: names the thread, then takes and runs jobs until stopped, signalling idle.
		@param index
			Worker number, used in the thread name
		*/
	void JobQueue::threadLoop(unsigned index)
	{
		profile::nameThread("Job worker", static_cast<int>(index));

		for (;;)
		{
			std::function<void()> job;

			{
				std::unique_lock<Mutex> lock(mutex_);
				wake_.wait(lock, [this] { return stop_ || !jobs_.empty(); });

				if (stop_)
					return;

				job = std::move(jobs_.front());
				jobs_.pop_front();
				++running_;
			}

			{
				// What the job IS shows up as the zones it opens itself; this
				// one separates work from waiting on the lane.
				VOX_PROFILE_SCOPE("Job");
				job();
			}

			{
				std::lock_guard<Mutex> lock(mutex_);
				--running_;
				if (jobs_.empty() && running_ == 0)
					idle_.notify_all();
			}
		}
	}

	/*!
		@brief
			Queues a callback for the next drain(). Safe from any thread.
		@param fn
			Callback to run on the main thread
		*/
	void MainThreadQueue::post(std::function<void()> fn)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		queue_.push_back(std::move(fn));
	}

	/*!
		@brief
			Swaps out the posted callbacks under the lock and runs them in order.
		@return
			Number of callbacks run
		*/
	std::size_t MainThreadQueue::drain()
	{
		std::deque<std::function<void()>> batch;

		{
			std::lock_guard<std::mutex> lock(mutex_);
			batch.swap(queue_);
		}

		for (auto& fn : batch)
			fn();

		return batch.size();
	}

	/*!
		@brief
			Discards every posted callback without running it.
		*/
	void MainThreadQueue::clear()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		queue_.clear();
	}
}
