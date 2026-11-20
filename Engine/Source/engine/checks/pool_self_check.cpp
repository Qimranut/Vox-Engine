/*************************************************************************************************************
\file       pool_self_check.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Startup self-check for util::memory::BlockPool (distinct aligned blocks, reuse, accounting,
            trim, thread safety) and for voxel Sections taking their dense data from the section pool.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "pool_self_check.hpp"

// Memory
#include "../../util/memory/block_pool.hpp"

// Voxel
#include "../../voxel/world/section.hpp"

// stds
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <set>
#include <thread>
#include <vector>


namespace engine::checks
{
	/*!
		@brief
			Exercises private block pools and the process-wide section pool and
			prints "[pool-check]" results. Never throws.
		*/
	void runPoolSelfCheck()
	{
		int failures = 0;

		const auto check = [&failures](bool ok, const char* what)
		{
			if (!ok)
			{
				std::cerr << "[pool-check] FAILED: " << what << "\n";
				++failures;
			}
		};

		try
		{
			using util::memory::BlockPool;

			// --------------------------- One pool ---------------------------
			//
			// Small blocks and small slabs, so a few dozen allocations cross
			// several slabs.
			{
				constexpr std::size_t kBlock = 100;		// rounds up to 112
				constexpr std::size_t kPerSlab = 8;

				BlockPool pool(kBlock, kPerSlab, "pool-check");
				check(pool.blockBytes() == 112, "the block size rounds up to the alignment");
				check(pool.stats().slabs_ == 0, "a pool reserves nothing until it is asked");

				std::vector<void*> blocks;
				for (int i = 0; i < 24; ++i)
					blocks.push_back(pool.allocate());

				const std::set<void*> distinct(blocks.begin(), blocks.end());
				check(distinct.size() == blocks.size(), "every block handed out is a different one");

				const bool aligned = std::all_of(blocks.begin(), blocks.end(),
					[](void* b) { return reinterpret_cast<std::uintptr_t>(b) % 16 == 0; });
				check(aligned, "blocks are 16-byte aligned");

				// Whole blocks: writing one to the brim leaves its neighbours alone.
				for (std::size_t i = 0; i < blocks.size(); ++i)
					std::memset(blocks[i], static_cast<int>(i + 1), pool.blockBytes());

				bool intact = true;
				for (std::size_t i = 0; i < blocks.size() && intact; ++i)
				{
					const auto* bytes = static_cast<const unsigned char*>(blocks[i]);
					intact = bytes[0] == i + 1 && bytes[pool.blockBytes() - 1] == i + 1;
				}
				check(intact, "blocks do not overlap");

				BlockPool::Stats s = pool.stats();
				check(s.blocksInUse_ == 24 && s.slabs_ == 3 && s.blocksReserved_ == 24, "24 blocks of 8 to a slab take three slabs");
				check(pool.owns(blocks[7]) && !pool.owns(&s), "owns() tells the pool's blocks from other memory");

				// Every slab is full. Give one back and ask again: the pool reuses it, no new slab.
				void* returned = blocks[5];
				pool.release(returned);
				void* again = pool.allocate();
				check(again == returned, "a released block is handed out again before a new slab is reserved");
				check(pool.stats().slabs_ == 3, "reuse does not reserve more");
				blocks[5] = again;

				// A slab with a block in use survives trim(); an empty one does not.
				for (std::size_t i = 1; i < blocks.size(); ++i)
					pool.release(blocks[i]);

				check(pool.stats().blocksInUse_ == 1, "releases are counted");

				const std::size_t released = pool.trim();
				s = pool.stats();
				check(s.slabs_ == 1 && released == 2 * kPerSlab * pool.blockBytes(), "trim() returns exactly the empty slabs");
				check(pool.owns(blocks[0]), "and leaves the slab that still has a block in use");

				const auto* first = static_cast<const unsigned char*>(blocks[0]);
				check(first[0] == 1 && first[pool.blockBytes() - 1] == 1, "whose contents are untouched");

				pool.release(blocks[0]);
				check(pool.trim(1) == 0 && pool.stats().slabs_ == 1, "trim(keep) keeps that many empty slabs");
				check(pool.trim() == kPerSlab * pool.blockBytes() && pool.stats().slabs_ == 0, "and trim() lets the last one go");
				check(pool.stats().blocksPeak_ == 24 && pool.stats().allocations_ == 25, "peak and lifetime counts are kept");

				pool.release(nullptr);	// harmless
			}

			// --------------------------- Threads ----------------------------
			//
			// Workers fill, mip and stream sections at once. Each thread writes
			// its own pattern into what it holds and must read it back: a block
			// handed to two threads shows up as a foreign byte.
			{
				constexpr int kThreads = 8;
				constexpr int kRounds = 400;
				constexpr int kHeld = 12;

				BlockPool pool(256, 16, "pool-check threads");
				std::atomic<int> corrupt{ 0 };

				std::vector<std::thread> threads;
				for (int t = 0; t < kThreads; ++t)
				{
					threads.emplace_back([&pool, &corrupt, t]
					{
						const auto pattern = static_cast<unsigned char>(t + 1);
						std::vector<void*> held;

						for (int round = 0; round < kRounds; ++round)
						{
							while (held.size() < kHeld)
							{
								void* block = pool.allocate();
								std::memset(block, pattern, 256);
								held.push_back(block);
							}

							// Release in a different order each round.
							const std::size_t drop = 1 + static_cast<std::size_t>(round * 7 + t) % (kHeld - 1);
							for (std::size_t i = 0; i < drop; ++i)
							{
								const std::size_t at = static_cast<std::size_t>(round + i * 5) % held.size();
								const auto* bytes = static_cast<const unsigned char*>(held[at]);

								if (bytes[8] != pattern || bytes[255] != pattern)
									corrupt.fetch_add(1);

								pool.release(held[at]);
								held.erase(held.begin() + static_cast<std::ptrdiff_t>(at));
							}
						}

						for (void* block : held)
							pool.release(block);
					});
				}

				for (std::thread& thread : threads)
					thread.join();

				check(corrupt.load() == 0, "no block is ever held by two threads at once");
				check(pool.stats().blocksInUse_ == 0, "every block came back from the threads");
				check(pool.stats().blocksPeak_ <= static_cast<std::size_t>(kThreads * kHeld), "and no more were out than the threads held");

				pool.trim();
				check(pool.stats().slabs_ == 0, "an idle pool trims to nothing");
			}

			// ------------------------- Voxel sections -----------------------
			//
			// The pool is process-wide, and a world may be loading on the
			// pipeline thread while this runs, so its counters are not this
			// check's to reason about: the accounting was proven on the private
			// pools above. Here it is about where a Section's voxels live.
			{
				BlockPool& pool = voxel::sectionPool();

				check(pool.blockBytes() == static_cast<std::size_t>(voxel::kSectionVolume), "the section pool's blocks are one section's voxels");

				voxel::Section dense = voxel::Section::dense();
				check(pool.owns(dense.data()) && dense.get(31, 31, 31) == voxel::kAir, "a dense section's voxels come from the pool, zeroed");

				voxel::Section uniform = voxel::Section::uniform(3);
				check(uniform.data() == nullptr, "a uniform section takes no block");

				uniform.set(1, 2, 3, 4);
				check(pool.owns(uniform.data()) && uniform.get(0, 0, 0) == 3 && uniform.get(1, 2, 3) == 4,
					"writing a different voxel promotes it to a pool block filled with its material");

				voxel::Section copy = uniform.clone();
				check(pool.owns(copy.data()) && copy.data() != uniform.data() && copy.get(1, 2, 3) == 4,
					"a clone has a pool block of its own");

				uniform.set(1, 2, 3, 3);
				check(uniform.tryCollapse() && uniform.data() == nullptr && uniform.uniformMaterial() == 3, "collapsing lets the block go");

				const std::uint8_t* held = copy.data();
				voxel::Section moved = std::move(copy);
				check(moved.data() == held && copy.data() == nullptr, "moving a section moves its block");

				moved.fill(voxel::kAir);
				check(moved.data() == nullptr && moved.isAir(), "fill() lets the block go");
			}
		}
		catch (const std::exception& e)
		{
			std::cerr << "[pool-check] threw: " << e.what() << "\n";
			++failures;
		}

		if (failures == 0)
			std::cout << "[pool-check] all checks passed\n";
		else
			std::cerr << "[pool-check] SOME CHECKS FAILED\n";
	}
}
