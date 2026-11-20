/*************************************************************************************************************
\file       block_pool.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares util::memory::BlockPool, a thread-safe pool of equally sized blocks carved out of
            large slabs, with exact usage stats and trim() to hand empty slabs back to the system.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Profiling
#include "../../profile/profiler.hpp"

// stds
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>


namespace util::memory
{
	/**
	 * A pool of equally sized blocks, carved out of large slabs.
	 *
	 * For data that comes and goes in one size and in large numbers - a voxel
	 * section's 32 KiB of materials is the case it was written for. Taking a
	 * block is a pop from a free list and giving it back a push, whatever the
	 * heap is doing; blocks of one slab sit side by side, so neighbours in
	 * time tend to be neighbours in memory; and the pool knows exactly how
	 * much it holds, which is what a memory budget needs to ask.
	 *
	 * Slabs are only handed back to the system by trim(): a pool that swings
	 * between full and half empty every few seconds (terrain streaming in and
	 * out) should not pay for the slabs again each time. Blocks are taken from
	 * the lowest slab that has one, so the high slabs are the ones that empty
	 * and can be trimmed.
	 *
	 * Thread-safe. Blocks come back uninitialised.
	 *
	 */
	class BlockPool
	{
	public:

		struct Stats
		{
			std::size_t		blockBytes_		{ 0 };
			std::size_t		blocksInUse_	{ 0 };
			std::size_t		blocksPeak_		{ 0 };	// most in use at once
			std::size_t		blocksReserved_	{ 0 };	// in use or free, across all slabs
			std::size_t		slabs_			{ 0 };
			std::uint64_t	allocations_	{ 0 };	// ever
			std::uint64_t	slabsCreated_	{ 0 };	// ever; allocations_ / slabsCreated_ is what the pool saved the heap

			/*!
				@brief
					Bytes in blocks currently handed out.
				@return
					blocksInUse_ * blockBytes_
				*/
			std::size_t bytesInUse() const		{ return blocksInUse_ * blockBytes_; }

			/*!
				@brief
					Bytes held in all slabs, used or free.
				@return
					blocksReserved_ * blockBytes_
				*/
			std::size_t bytesReserved() const	{ return blocksReserved_ * blockBytes_; }
		};

		/*!
			@brief
				Creates an empty pool; no slab is allocated until the first allocate().
			@param blockBytes
				Size of every block; at least a pointer, and rounded up to a multiple of 16
			@param blocksPerSlab
				How many blocks one slab holds; at least 1
			@param name
				For the profiler; must outlive the pool (a literal)
			*/
		BlockPool(std::size_t blockBytes, std::size_t blocksPerSlab, const char* name);

		/*!
			@brief
				Frees every slab; asserts that no block is still in use.
			*/
		~BlockPool();

		BlockPool(const BlockPool&) = delete;
		BlockPool& operator=(const BlockPool&) = delete;

		/*!
			@brief
				Takes a block from the lowest slab that has one, adding a slab if none does.
			@return
				A block of blockBytes() bytes, 16-byte aligned, uninitialised. Throws
				std::bad_alloc when the system has no memory for another slab.
			*/
		void* allocate();

		/*!
			@brief
				Gives a block back.

				Null is ignored; a pointer that is not one of this pool's blocks is
				a bug and asserts.
			@param block
				Block returned by allocate()
			*/
		void release(void* block) noexcept;

		/*!
			@brief
				Returns every slab that has no block in use to the system.
			@param keepSlabs
				Empty slabs to keep anyway, so the next allocations do not start by
				asking the system for one
			@return
				Bytes handed back
			*/
		std::size_t trim(std::size_t keepSlabs = 0);

		/*!
			@brief
				Snapshot of the pool's usage counters.
			@return
				The current stats
			*/
		Stats stats() const;

		/*!
			@brief
				Size of every block.
			@return
				Block size in bytes, after rounding
			*/
		std::size_t blockBytes() const { return blockBytes_; }

		/*!
			@brief
				The pool's profiler name.
			@return
				The name given at construction
			*/
		const char* name() const { return name_; }

		/*!
			@brief
				Whether a pointer lies inside one of this pool's slabs.
			@param block
				Pointer to test
			@return
				true when a slab of this pool contains it
			*/
		bool owns(const void* block) const;

	private:

		struct Slab
		{
			std::byte*	base_	{ nullptr };
			void*		free_	{ nullptr };	// intrusive list through the blocks given back
			std::size_t	fresh_	{ 0 };			// blocks below this index have been handed out at least once
			std::size_t	used_	{ 0 };
		};

		/*!
			@brief
				Finds the slab containing a pointer. Caller holds the lock.
			@param block
				Pointer to look up
			@return
				The slab, or null when no slab contains it
			*/
		Slab* slabOf(const void* block);

		/*!
			@brief
				Finds the slab containing a pointer by bisection. Caller holds the lock.
			@param block
				Pointer to look up
			@return
				The slab, or null when no slab contains it
			*/
		const Slab* slabOf(const void* block) const;

		/*!
			@brief
				Allocates a new slab and inserts it in address order. Caller holds the lock.
			*/
		void addSlab();

		const std::size_t	blockBytes_;
		const std::size_t	blocksPerSlab_;
		const char* const	name_;

		mutable VOX_PROFILE_LOCKABLE(std::mutex, mutex_, "BlockPool");

		// Sorted by base address, so release() finds a block's slab by
		// bisection. Slabs are pointers because the vector moves.
		std::vector<std::unique_ptr<Slab>>	slabs_;

		// No slab below this index has a free block.
		std::size_t		firstFree_		{ 0 };

		std::size_t		blocksInUse_	{ 0 };
		std::size_t		blocksPeak_		{ 0 };
		std::uint64_t	allocations_	{ 0 };
		std::uint64_t	slabsCreated_	{ 0 };
	};
}
