/*************************************************************************************************************
\file       quad_arena.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Implements QuadArena's first-fit allocation over a sorted free list, merging freed ranges
            with their neighbours.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "quad_arena.hpp"

// stds
#include <algorithm>


namespace voxel::render
{
	/*!
		@brief
			Forgets every allocation and makes the whole capacity one free range.
		@param capacityQuads
			New capacity in quads
		*/
	void QuadArena::reset(std::uint32_t capacityQuads)
	{
		capacity_ = capacityQuads;
		used_ = 0;
		free_.clear();
		if (capacity_ > 0)
			free_.push_back({ 0, capacity_ });
	}

	/*!
		@brief
			Adds slots at the end, extending a trailing free range when there is one.
		@param capacityQuads
			New capacity in quads; ignored unless larger than the current one
		*/
	void QuadArena::grow(std::uint32_t capacityQuads)
	{
		if (capacityQuads <= capacity_)
			return;

		const std::uint32_t added = capacityQuads - capacity_;
		if (!free_.empty() && free_.back().offset_ + free_.back().count_ == capacity_)
			free_.back().count_ += added;
		else
			free_.push_back({ capacity_, added });
		capacity_ = capacityQuads;
	}

	/*!
		@brief
			Takes quads slots from the first free range large enough.
		@param quads
			Number of quads; 0 returns offset 0
		@return
			First quad slot, or kNone when the arena is full
		*/
	std::uint32_t QuadArena::allocate(std::uint32_t quads)
	{
		if (quads == 0)
			return 0;

		for (std::size_t i = 0; i < free_.size(); ++i)
		{
			Range& r = free_[i];
			if (r.count_ < quads)
				continue;

			const std::uint32_t offset = r.offset_;
			r.offset_ += quads;
			r.count_ -= quads;
			if (r.count_ == 0)
				free_.erase(free_.begin() + static_cast<std::ptrdiff_t>(i));

			used_ += quads;
			return offset;
		}

		return kNone;
	}

	/*!
		@brief
			Inserts a range into the sorted free list and merges it with both neighbours.
		@param offset
			First quad slot of the range; kNone is ignored
		@param quads
			Number of quads
		*/
	void QuadArena::free(std::uint32_t offset, std::uint32_t quads)
	{
		if (quads == 0 || offset == kNone)
			return;

		used_ -= std::min(used_, quads);

		// Insert sorted, then merge with both neighbours.
		const auto it = std::lower_bound(free_.begin(), free_.end(), offset,
			[](const Range& r, std::uint32_t o) { return r.offset_ < o; });

		const std::size_t index = static_cast<std::size_t>(it - free_.begin());
		free_.insert(it, { offset, quads });

		if (index + 1 < free_.size() && free_[index].offset_ + free_[index].count_ == free_[index + 1].offset_)
		{
			free_[index].count_ += free_[index + 1].count_;
			free_.erase(free_.begin() + static_cast<std::ptrdiff_t>(index + 1));
		}

		if (index > 0 && free_[index - 1].offset_ + free_[index - 1].count_ == free_[index].offset_)
		{
			free_[index - 1].count_ += free_[index].count_;
			free_.erase(free_.begin() + static_cast<std::ptrdiff_t>(index));
		}
	}
}
