/*************************************************************************************************************
\file       edit_store.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      EditStore: keeps the compressed hand-edited sections of streamed-out cells, under a byte
            budget, so the cell's next build puts them back.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Voxel
#include "../edit/edit_history.hpp"
#include "../gen/stream/tile_generator.hpp"

// GLM
#include <glm/glm.hpp>

// stds
#include <cstddef>
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <vector>


namespace voxel::stream
{
	/**
	 * The hand-edited sections of cells that have streamed out. A streamed
	 * terrain can always be generated again, but not what a brush or a blast
	 * did to it: when a cell is let go, its level-0 sections that were ever
	 * edited are kept here, compressed with the map file's section codec (a few
	 * KB each), and handed to the worker that next builds the cell, which puts
	 * them in place of the generated sections and builds the LoD levels on top.
	 *
	 * Under a byte budget: past it the oldest edits are forgotten, and that
	 * ground comes back as generated. Lives for the session; an endless level
	 * saves as its recipe alone. Main thread only - collect() copies what a
	 * worker needs.
	 *
	 */
	class EditStore
	{
	public:

		/*!
			@brief
				Creates an empty store with a byte budget.
			@param budgetBytes
				Compressed bytes to keep before the oldest edits are forgotten
			*/
		explicit EditStore(std::size_t budgetBytes = std::size_t{ 64 } << 20) : budget_(budgetBytes) {}

		/*!
			@brief
				Changes the byte budget and forgets the oldest edits if now over it.
			@param bytes
				New budget in bytes
			*/
		void setBudget(std::size_t bytes) { budget_ = bytes; trim(); }

		/*!
			@brief
				Keeps a section, replacing what was kept for it.
			@param index
				World::sectionIndex(0, ...) of the section
			@param section
				Level-0 section coordinate
			@param snapshot
				Compressed voxels of the section
			*/
		void put(std::size_t index, const glm::ivec3& section, edit::SectionSnapshot snapshot);

		/*!
			@brief
				Forgets a section: it is back in the world.
			@param index
				World::sectionIndex(0, ...) of the section
			*/
		void erase(std::size_t index);

		/*!
			@brief
				Reports whether a section is kept.
			@param index
				World::sectionIndex(0, ...) of the section
			@return
				true when kept
			*/
		bool contains(std::size_t index) const { return entries_.find(index) != entries_.end(); }

		/*!
			@brief
				Copies of what is kept for one cell (World::kCellSections sections a side).
			@param cell
				Cell coordinate
			@return
				The kept sections of that cell
			*/
		std::vector<StoredSection> collect(const glm::ivec2& cell) const;

		/*!
			@brief
				Forgets every kept section.
			*/
		void clear();

		/*!
			@brief
				Number of sections kept.
			@return
				Section count
			*/
		std::size_t size() const { return entries_.size(); }

		/*!
			@brief
				Compressed bytes kept.
			@return
				Byte count
			*/
		std::size_t bytes() const { return bytes_; }

		/*!
			@brief
				The byte budget.
			@return
				Budget in bytes
			*/
		std::size_t budget() const { return budget_; }

		/*!
			@brief
				Edits forgotten for space, ever.
			@return
				Count of forgotten sections
			*/
		std::uint64_t forgotten() const { return forgotten_; }

	private:

		struct Entry
		{
			glm::ivec3				section_	{ 0 };
			edit::SectionSnapshot	snapshot_;
			std::uint64_t			stamp_		{ 0 };
		};

		/*!
			@brief
				Forgets the oldest kept sections until within budget, always keeping the newest.
			*/
		void trim();

		std::unordered_map<std::size_t, Entry>				entries_;
		std::deque<std::pair<std::size_t, std::uint64_t>>	order_;		// (index, stamp), oldest first; stale pairs are skipped
		std::size_t		bytes_		{ 0 };
		std::size_t		budget_;
		std::uint64_t	clock_		{ 0 };
		std::uint64_t	forgotten_	{ 0 };
	};
}
