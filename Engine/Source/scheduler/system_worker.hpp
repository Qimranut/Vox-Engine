#pragma once

// System interface
#include "sys_interface.hpp"

// Profiling
#include "../profile/profiler.hpp"

// stds
#include <cstdio>
#include <cstring>
#include <thread>
#include <mutex>
#include <condition_variable>


namespace scheduler
{
	class SystemWorker
	{
	public:

		SystemWorker(ISystem* sys)
			:
			sys_{ sys },
			running_{ true }
		{
			thread_ = std::thread(&SystemWorker::threadLoop, this);
		}

		~SystemWorker()
		{
			shutdown();
		}
		
		/**
		 * Caleld by scheduler to tell this worker's thread to start their update
		 * 
		 * [param] dt
		 */
		void signalStart(float dt)
		{
			{
				// Hold the lock while touching shared data
				std::lock_guard<std::mutex> lock{ mtx_ };
				deltaTime_ = dt;
				hasWork_ = true;
			}

			// Wake up worker thread if its asleep in cv.wait()
			cv_.notify_one();
		}

		/**
		 * Called by scheduler to block until current system's update has completed
		 * 
		 */
		void waitUntilFinished()
		{
			// Sleep until 
			std::unique_lock<std::mutex> lock{ mtx_ };
			cv_.wait(lock, [this] { return !hasWork_; });
		}

		void shutdown()
		{
			{
				// Need to set hasWork = true as threadLoop is asleep,
				// waiting for hasWork == ture. If only running = false,
				// the thread would never wake up to see that it is false.
				std::lock_guard<std::mutex> lock{ mtx_ };
				running_ = false;
				hasWork_ = true;
			}

			cv_.notify_one();
			if (thread_.joinable()) thread_.join();
		}

	private:

		ISystem* sys_;
		std::thread thread_;
		std::mutex mtx_;
		std::condition_variable cv_;

		bool hasWork_			{ false };
		bool running_			{ false };
		float deltaTime_		{ 0.f };

		void threadLoop()
		{
			// One lane per scheduled system, named after it.
			const char* name = sys_ ? sys_->getName() : "idle";
			char lane[64]{};
			std::snprintf(lane, sizeof(lane), "System: %s", name ? name : "unnamed");
			profile::nameThread(lane);

			while (true)
			{
				std::unique_lock<std::mutex> lock{ mtx_ };

				// Sleep until scheduler signals
				cv_.wait(lock, [this] { return hasWork_; });

				// Shutdown requested
				if (!running_)
					break;

				// Do actual work by unlocking
				lock.unlock();
				{
					VOX_PROFILE_SCOPE("System update");
					VOX_PROFILE_TEXT(lane, std::strlen(lane));
					sys_->update(deltaTime_);
				}
				lock.lock();

				// Inform scheduler that work is done
				hasWork_ = false;
				lock.unlock();
				cv_.notify_all();
			}
		}

	};
}
