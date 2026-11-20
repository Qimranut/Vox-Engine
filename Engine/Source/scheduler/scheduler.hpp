#pragma once

// System worker
#include "system_worker.hpp"

// stds
#include <vector>


namespace scheduler
{
	class Scheduler
	{
	public:
		
		/**
		 * Registers a system with the scheduler.
		 * These dependencies represent other system which must finish
		 * their update() before this one is allowed to start
		 * e.g (graphicsWorker, { physicsWorker} )
		 * Graphics depends on physics, so graphics will not start until physics is done
		 * 
		 * [param] worker
		 * [param] dependencies
		 */
		void registerSys(SystemWorker* worker, std::vector<SystemWorker*> dependencies)
		{
			entries_.push_back({ worker, dependencies });
		}

		/**
		 * Called once per frame by main engine loop
		 * 
		 * [param] dt
		 * deltaTime
		 * 
		 */
		void runFrame(float dt)
		{
			// Start off systems with no dependencies immediately
			// as these can run in parallel
			for (auto& entry : entries_)
			{
				if (entry.dependencies_.empty())
					entry.worker_->signalStart(dt);
			}

			// For systems with dependencies, wait for dependencies to finish
			// their operations, before starting these systems
			
			// Using launcher threads to manage workers with dependencies
			// so that workers with non-related dependencies do not hold
			// up the rest. Without this, main thread will be stuck on waitUntilFinished.
			std::vector<std::thread> launchers;
			for (auto& entry : entries_)
			{
				if (!entry.dependencies_.empty())
				{
					launchers.emplace_back([&entry, dt]
						{
							for (auto* dep : entry.dependencies_)
							{
								dep->waitUntilFinished();
							}

							entry.worker_->signalStart(dt);
						});
				}
			}
			
			// Ensure that all launcher threads have fired signalStart before moving on
			for (auto& l : launchers)
			{
				l.join();
			}

			//for (auto& entry : entries_)
			//{
			//	if (!entry.dependencies_.empty())
			//	{
			//		for (auto* dep : entry.dependencies_)
			//		{
			//			dep->waitUntilFinished();
			//		}

			//		entry.worker_->signalStart(dt);
			//	}
			//}

			// Before ending the frame, ensure that all systems have finished operations
			// Main thread should not move on to the next frame while there are running workers
			for (auto& entry : entries_)
			{
				entry.worker_->waitUntilFinished();
			}
		}

	private:

		struct Entry
		{
			SystemWorker* worker_;
			std::vector<SystemWorker*> dependencies_;
		};

		std::vector <Entry> entries_;
	};
}
