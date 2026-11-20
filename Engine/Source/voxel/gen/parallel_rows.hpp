/*************************************************************************************************************
\file       parallel_rows.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares forRows, which runs a per-row function over a grid in bands spread across a
            JobQueue, for generator passes whose rows write only themselves.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Threads
#include "../../util/thread/job_queue.hpp"

// stds
#include <algorithm>


namespace voxel
{
	/*!
		@brief
			Calls fn(z) for every row in [0, depth) and returns once all rows are done.

			Rows run in bands spread over the queue when one is given. Only for
			passes where a row reads shared data and writes nothing but itself:
			then the result is the same on any number of threads.

			Never call it from a job - it waits for the queue to go idle.
		@param jobs
			Queue to spread the bands over, or null to run on this thread
		@param depth
			Number of rows
		@param fn
			Called once per row with the row index
		*/
	template <typename Fn>
	void forRows(util::thread::JobQueue* jobs, int depth, const Fn& fn)
	{
		if (jobs == nullptr)
		{
			for (int z = 0; z < depth; ++z)
				fn(z);
			return;
		}

		constexpr int kBand = 16;
		for (int z0 = 0; z0 < depth; z0 += kBand)
		{
			const int z1 = std::min(z0 + kBand, depth);
			jobs->push([&fn, z0, z1]
			{
				for (int z = z0; z < z1; ++z)
					fn(z);
			});
		}

		jobs->waitIdle();
	}
}
