/*************************************************************************************************************
\file       block_pool.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Implements BlockPool: address-ordered slabs with per-slab intrusive free lists, lowest-slab-
            first allocation, release by bisection, trim of empty slabs and usage stats.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "block_pool.hpp"

// stds
#include <algorithm>
#include <cassert>
#include <new>


namespace util::memory
{
	namespace
	{
		constexpr std::size_t kBlockAlignment = 16;

		// Slabs are large; a cache line keeps the first block clear of whatever
		// the heap put in front of it.
		constexpr std::align_val_t kSlabAlignment{ 64 };

		/*!
			@brief
				Rounds a value up to a multiple.
			@param value
				Value to round
			@param multiple
				The multiple, non-zero
			@return
				The smallest multiple of multiple not below value
			*/
		std::size_t roundUp(std::size_t value, std::size_t multiple)
		{
			return (value + multiple - 1) / multiple * multiple;
		}
	}

	/*!
		@brief
			Creates an empty pool with the block size rounded up to 16 bytes.
		@param blockBytes
			Size of every block; at least a pointer
		@param blocksPerSlab
			How many blocks one slab holds; at least 1
		@param name
			For the profiler; must outlive the pool
		*/
	BlockPool::BlockPool(std::size_t blockBytes, std::size_t blocksPerSlab, const char* name)
		: blockBytes_{ roundUp(std::max(blockBytes, sizeof(void*)), kBlockAlignment) }
		, blocksPerSlab_{ std::max<std::size_t>(blocksPerSlab, 1) }
		, name_{ name }
	{
	}

	/*!
		@brief
			Frees every slab; asserts that no block is still in use.
		*/
	BlockPool::~BlockPool()
	{
		// Blocks still out are their owners' problem; the memory goes either way.
		assert(blocksInUse_ == 0 && "BlockPool destroyed with blocks still in use");

		for (const std::unique_ptr<Slab>& slab : slabs_)
			::operator delete(slab->base_, kSlabAlignment);
	}

	/*!
		@brief
			Allocates a new slab, inserts it in address order and lowers firstFree_ if needed.
		*/
	void BlockPool::addSlab()
	{
		VOX_PROFILE_SCOPE_C("BlockPool new slab", profile::colour::kResource);

		auto slab = std::make_unique<Slab>();
		slab->base_ = static_cast<std::byte*>(::operator new(blockBytes_ * blocksPerSlab_, kSlabAlignment));

		const auto at = std::upper_bound(slabs_.begin(), slabs_.end(), slab->base_,
			[](const std::byte* base, const std::unique_ptr<Slab>& other) { return base < other->base_; });

		// Every slab below firstFree_ is full and stays where it is, so the new
		// one only matters when it lands at or below that index.
		const std::size_t index = static_cast<std::size_t>(at - slabs_.begin());
		slabs_.insert(at, std::move(slab));

		if (index <= firstFree_)
			firstFree_ = index;

		++slabsCreated_;
	}

	/*!
		@brief
			Takes a block from the lowest slab with room: a freed one first,
			otherwise the slab's next never-used block.
		@return
			The block, uninitialised
		*/
	void* BlockPool::allocate()
	{
		std::lock_guard<VOX_PROFILE_LOCKABLE_TYPE(std::mutex)> lock(mutex_);

		while (firstFree_ < slabs_.size() && slabs_[firstFree_]->used_ == blocksPerSlab_)
			++firstFree_;

		if (firstFree_ == slabs_.size())
		{
			addSlab();

			// The new slab may sit anywhere in address order; firstFree_ only
			// moved if it sits below. Either way the search starts over.
			while (slabs_[firstFree_]->used_ == blocksPerSlab_)
				++firstFree_;
		}

		Slab& slab = *slabs_[firstFree_];
		void* block = nullptr;

		if (slab.free_)
		{
			block = slab.free_;
			slab.free_ = *static_cast<void**>(block);
		}
		else
		{
			// Never handed out yet. Blocks are taken in order and only the
			// freed ones are linked, so a fresh slab is not touched end to end
			// just to build a list.
			assert(slab.fresh_ < blocksPerSlab_);
			block = slab.base_ + slab.fresh_ * blockBytes_;
			++slab.fresh_;
		}

		++slab.used_;
		++blocksInUse_;
		++allocations_;
		blocksPeak_ = std::max(blocksPeak_, blocksInUse_);

		return block;
	}

	/*!
		@brief
			Pushes a block onto its slab's free list. Null is ignored.
		@param block
			Block returned by allocate()
		*/
	void BlockPool::release(void* block) noexcept
	{
		if (!block)
			return;

		std::lock_guard<VOX_PROFILE_LOCKABLE_TYPE(std::mutex)> lock(mutex_);

		Slab* slab = slabOf(block);
		assert(slab && "BlockPool::release: not a block of this pool");
		if (!slab)
			return;

		assert((static_cast<std::byte*>(block) - slab->base_) % blockBytes_ == 0 && "BlockPool::release: not the start of a block");
		assert(slab->used_ > 0);

		*static_cast<void**>(block) = slab->free_;
		slab->free_ = block;
		--slab->used_;
		--blocksInUse_;

		// Bisect again for the index: cheaper than carrying it around, and
		// release is already paying for one bisection.
		const auto at = std::lower_bound(slabs_.begin(), slabs_.end(), slab->base_,
			[](const std::unique_ptr<Slab>& other, const std::byte* base) { return other->base_ < base; });

		firstFree_ = std::min(firstFree_, static_cast<std::size_t>(at - slabs_.begin()));
	}

	/*!
		@brief
			Frees every empty slab beyond the first keepSlabs empty ones.
		@param keepSlabs
			Empty slabs to keep
		@return
			Bytes handed back
		*/
	std::size_t BlockPool::trim(std::size_t keepSlabs)
	{
		std::lock_guard<VOX_PROFILE_LOCKABLE_TYPE(std::mutex)> lock(mutex_);

		std::size_t released = 0;
		std::size_t kept = 0;

		std::erase_if(slabs_, [&](const std::unique_ptr<Slab>& slab)
		{
			if (slab->used_ != 0)
				return false;

			if (kept < keepSlabs)
			{
				++kept;
				return false;
			}

			::operator delete(slab->base_, kSlabAlignment);
			released += blockBytes_ * blocksPerSlab_;
			return true;
		});

		firstFree_ = 0;
		return released;
	}

	/*!
		@brief
			Snapshot of the pool's usage counters, taken under the lock.
		@return
			The current stats
		*/
	BlockPool::Stats BlockPool::stats() const
	{
		std::lock_guard<VOX_PROFILE_LOCKABLE_TYPE(std::mutex)> lock(mutex_);

		Stats s;
		s.blockBytes_ = blockBytes_;
		s.blocksInUse_ = blocksInUse_;
		s.blocksPeak_ = blocksPeak_;
		s.blocksReserved_ = slabs_.size() * blocksPerSlab_;
		s.slabs_ = slabs_.size();
		s.allocations_ = allocations_;
		s.slabsCreated_ = slabsCreated_;
		return s;
	}

	/*!
		@brief
			Whether a pointer lies inside one of this pool's slabs.
		@param block
			Pointer to test
		@return
			true when a slab of this pool contains it
		*/
	bool BlockPool::owns(const void* block) const
	{
		std::lock_guard<VOX_PROFILE_LOCKABLE_TYPE(std::mutex)> lock(mutex_);
		return slabOf(block) != nullptr;
	}

	/*!
		@brief
			Finds the slab containing a pointer by bisection over base addresses.
		@param block
			Pointer to look up
		@return
			The slab, or null when no slab contains it
		*/
	const BlockPool::Slab* BlockPool::slabOf(const void* block) const
	{
		const auto* address = static_cast<const std::byte*>(block);

		// The first slab that starts after the block; the one before it is the
		// only one that can hold it.
		const auto after = std::upper_bound(slabs_.begin(), slabs_.end(), address,
			[](const std::byte* a, const std::unique_ptr<Slab>& slab) { return a < slab->base_; });

		if (after == slabs_.begin())
			return nullptr;

		const Slab& slab = **(after - 1);
		return address < slab.base_ + blockBytes_ * blocksPerSlab_ ? &slab : nullptr;
	}

	/*!
		@brief
			Non-const overload of slabOf, forwarding to the const one.
		@param block
			Pointer to look up
		@return
			The slab, or null when no slab contains it
		*/
	BlockPool::Slab* BlockPool::slabOf(const void* block)
	{
		return const_cast<Slab*>(static_cast<const BlockPool&>(*this).slabOf(block));
	}
}
