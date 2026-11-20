/*************************************************************************************************************
\file       edit_store.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Implements EditStore: keeping, collecting per cell and budget-trimming the compressed edited
            sections of streamed-out cells.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "edit_store.hpp"

// Voxel
#include "../world/world.hpp"

// stds
#include <utility>


namespace voxel::stream
{
	/*!
		@brief
			Keeps a section, replacing what was kept for it, then trims to the budget.
		@param index
			World::sectionIndex(0, ...) of the section
		@param section
			Level-0 section coordinate
		@param snapshot
			Compressed voxels of the section
		*/
	void EditStore::put(std::size_t index, const glm::ivec3& section, edit::SectionSnapshot snapshot)
	{
		erase(index);

		Entry entry;
		entry.section_ = section;
		entry.snapshot_ = std::move(snapshot);
		entry.stamp_ = ++clock_;

		bytes_ += entry.snapshot_.bytes();
		order_.emplace_back(index, entry.stamp_);
		entries_.emplace(index, std::move(entry));

		trim();
	}

	/*!
		@brief
			Forgets a section: it is back in the world.
		@param index
			World::sectionIndex(0, ...) of the section
		*/
	void EditStore::erase(std::size_t index)
	{
		const auto found = entries_.find(index);
		if (found == entries_.end())
			return;

		bytes_ -= found->second.snapshot_.bytes();
		entries_.erase(found);
	}

	/*!
		@brief
			Copies of what is kept for one cell.
		@param cell
			Cell coordinate
		@return
			The kept sections of that cell
		*/
	std::vector<StoredSection> EditStore::collect(const glm::ivec2& cell) const
	{
		std::vector<StoredSection> out;

		for (const auto& [index, entry] : entries_)
		{
			if (World::cellOf(0, entry.section_.x, entry.section_.z) != cell)
				continue;

			StoredSection stored;
			stored.section_ = entry.section_;
			stored.snapshot_ = entry.snapshot_;
			out.push_back(std::move(stored));
		}

		return out;
	}

	/*!
		@brief
			Forgets every kept section.
		*/
	void EditStore::clear()
	{
		entries_.clear();
		order_.clear();
		bytes_ = 0;
	}

	/*!
		@brief
			Forgets the oldest kept sections until within budget, always keeping the newest.
		*/
	void EditStore::trim()
	{
		// The newest always stays, as in the undo history: forgetting the
		// edit that was just put away would be worse than going over.
		while (bytes_ > budget_ && entries_.size() > 1 && !order_.empty())
		{
			const auto [index, stamp] = order_.front();
			order_.pop_front();

			const auto found = entries_.find(index);
			if (found == entries_.end() || found->second.stamp_ != stamp)
				continue;	// replaced or taken back since

			bytes_ -= found->second.snapshot_.bytes();
			entries_.erase(found);
			++forgotten_;
		}

		// Nothing left to point at: the queue is only stale pairs.
		if (entries_.empty())
			order_.clear();
	}
}
