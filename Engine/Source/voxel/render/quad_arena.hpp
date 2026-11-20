/*************************************************************************************************************
\file       quad_arena.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      QuadArena: CPU-side first-fit allocator of quad slots in the voxel renderer's GPU buffer,
            with merging frees and in-place growth.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// stds
#include <cstddef>
#include <cstdint>
#include <vector>


namespace voxel::render
{
	/**
	 * First-fit allocator over quad slots of one big GPU buffer. Frees
	 * merge with their neighbours, so a world that is remeshed section by
	 * section does not fragment into unusable holes.
	 *
	 * CPU bookkeeping only; the caller uploads into the ranges it hands
	 * out.
	 *
	 */
	class QuadArena
	{
	public:

		static constexpr std::uint32_t kNone = 0xFFFFFFFFu;

		/*!
			@brief
				Creates an arena with the given number of free quad slots.
			@param capacityQuads
				Capacity in quads
			*/
		explicit QuadArena(std::uint32_t capacityQuads = 0) { reset(capacityQuads); }

		/*!
			@brief
				Forgets every allocation and makes the whole capacity one free range.
			@param capacityQuads
				New capacity in quads
			*/
		void reset(std::uint32_t capacityQuads);

		/*!
			@brief
				Adds slots at the end; every range handed out keeps its offset.

				Does nothing unless capacityQuads is larger than capacity().
			@param capacityQuads
				New capacity in quads
			*/
		void grow(std::uint32_t capacityQuads);

		/*!
			@brief
				Allocates a contiguous range of quad slots, first fit.
			@param quads
				Number of quads
			@return
				First quad slot, or kNone when the arena is full
			*/
		std::uint32_t allocate(std::uint32_t quads);

		/*!
			@brief
				Returns a range to the arena, merging it with adjacent free ranges.
			@param offset
				First quad slot of the range; kNone is ignored
			@param quads
				Number of quads
			*/
		void free(std::uint32_t offset, std::uint32_t quads);

		/*!
			@brief
				Total number of quad slots.
			@return
				Capacity in quads
			*/
		std::uint32_t capacity() const { return capacity_; }

		/*!
			@brief
				Number of quad slots currently allocated.
			@return
				Used quads
			*/
		std::uint32_t used() const { return used_; }

	private:

		struct Range
		{
			std::uint32_t offset_;
			std::uint32_t count_;
		};

		std::vector<Range>	free_;		// sorted by offset
		std::uint32_t		capacity_	{ 0 };
		std::uint32_t		used_		{ 0 };
	};
}
