/*************************************************************************************************************
\file       edit_history.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Section snapshots through the voxmap section codec, and the History transaction stack:
            begin / touch / commit, rollback of an open transaction, undo, redo and budget eviction.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "edit_history.hpp"

// Voxel
#include "../io/voxmap_codec.hpp"

// stds
#include <algorithm>
#include <cstring>
#include <stdexcept>


namespace voxel::edit
{
	/*!
		@brief
			Captures a section: its material if uniform, otherwise its voxels encoded with the map codec.
		@param section
			Section to capture
		@return
			The snapshot
		*/
	SectionSnapshot SectionSnapshot::capture(const Section& section)
	{
		SectionSnapshot s;
		if (section.isUniform())
		{
			s.material_ = section.uniformMaterial();
			return s;
		}

		s.uniform_ = false;
		s.codec_ = voxmap::encodeSection(section.data(), s.payload_);
		s.payload_.shrink_to_fit();
		return s;
	}

	/*!
		@brief
			Rebuilds the section, decoding the payload with runtime ids; throws if it does not decode.
		@return
			The restored section, collapsed to uniform when possible
		*/
	Section SectionSnapshot::restore() const
	{
		if (uniform_)
			return Section::uniform(material_);

		// The palette holds runtime ids already: nothing was written to a file.
		Section section = Section::dense();
		const bool ok = voxmap::decodeSection(payload_, codec_,
			[](std::uint16_t id) { return static_cast<MaterialId>(id); }, section.data());
		if (!ok)
			throw std::logic_error("edit history: a section snapshot did not decode");

		section.tryCollapse();
		return section;
	}

	/*!
		@brief
			Creates an empty history.
		@param budgetBytes
			Snapshot memory above which the oldest transactions are evicted
		*/
	History::History(std::size_t budgetBytes)
		: budget_(budgetBytes)
	{
	}

	/*!
		@brief
			Opens a transaction unless one is already open.
		@param id
			Caller-chosen nonzero id
		@param label
			Name shown for the transaction
		*/
	void History::begin(Id id, std::string label)
	{
		if (open_)
			return;

		current_ = Transaction{};
		current_.id_ = id;
		current_.label_ = std::move(label);
		open_ = true;
	}

	/*!
		@brief
			Records a section's before-state the first time the open transaction touches it.
		@param section
			Level-0 section coordinate
		@param before
			The section before the change
		*/
	void History::touch(const glm::ivec3& section, const Section& before)
	{
		if (!open_)
			return;

		const auto same = [&](const Entry& e) { return e.section_ == section; };
		if (std::any_of(current_.entries_.begin(), current_.entries_.end(), same))
			return;

		Entry e;
		e.section_ = section;
		e.before_ = SectionSnapshot::capture(before);
		current_.entries_.push_back(std::move(e));
	}

	/*!
		@brief
			Closes the open transaction, capturing after-states, clearing redo and evicting over budget.
		@param world
			World to read the after-state from
		@return
			Whether a transaction was recorded (false if none was open or it touched nothing)
		*/
	bool History::commit(const World& world)
	{
		if (!open_)
			return false;
		open_ = false;

		if (current_.entries_.empty())
			return false;

		for (Entry& e : current_.entries_)
		{
			e.after_ = SectionSnapshot::capture(world.section(0, e.section_.x, e.section_.y, e.section_.z));
			current_.bytes_ += e.before_.bytes() + e.after_.bytes();
		}

		for (const Transaction& t : redo_)
			bytes_ -= t.bytes_;
		redo_.clear();

		bytes_ += current_.bytes_;
		undo_.push_back(std::move(current_));
		current_ = Transaction{};

		evict();
		return true;
	}

	/*!
		@brief
			Restores the open transaction's sections to their before-state and forgets them.
		@param world
			World to write to
		@return
			The sections that were restored; empty if no transaction is open
		*/
	std::vector<glm::ivec3> History::rollbackOpen(World& world)
	{
		if (!open_)
			return {};

		std::vector<glm::ivec3> restored = restore(world, current_, true);
		current_.entries_.clear();
		return restored;
	}

	/*!
		@brief
			Whether a transaction is on the undo or redo stack.
		@param id
			Transaction id
		@return
			True when found
		*/
	bool History::contains(Id id) const
	{
		const auto has = [id](const Transaction& t) { return t.id_ == id; };
		return std::any_of(undo_.begin(), undo_.end(), has) || std::any_of(redo_.begin(), redo_.end(), has);
	}

	/*!
		@brief
			Label of the transaction undo() would revert.
		@return
			The label, empty if none
		*/
	const std::string& History::undoLabel() const
	{
		static const std::string none;
		return undo_.empty() ? none : undo_.back().label_;
	}

	/*!
		@brief
			Label of the transaction redo() would re-apply.
		@return
			The label, empty if none
		*/
	const std::string& History::redoLabel() const
	{
		static const std::string none;
		return redo_.empty() ? none : redo_.back().label_;
	}

	/*!
		@brief
			Writes a transaction's before or after snapshots back; non-resident ones go to the offline sink.
		@param world
			World to write to
		@param t
			Transaction to restore
		@param before
			True for the before-state, false for the after-state
		@return
			The resident sections written
		*/
	std::vector<glm::ivec3> History::restore(World& world, const Transaction& t, bool before) const
	{
		std::vector<glm::ivec3> sections;
		sections.reserve(t.entries_.size());
		for (const Entry& e : t.entries_)
		{
			const SectionSnapshot& snapshot = before ? e.before_ : e.after_;

			// Streamed away since the edit: there is no section to put it back
			// into, and writing one would make the cell look resident. Whoever
			// keeps evicted edits takes the snapshot, and the section is not
			// reported - nothing of it is in the world to re-mip or remesh.
			if (!world.sectionResident(0, e.section_.x, e.section_.y, e.section_.z))
			{
				if (offline_)
					offline_(e.section_, snapshot);
				continue;
			}

			world.editSection(0, e.section_.x, e.section_.y, e.section_.z) = snapshot.restore();
			sections.push_back(e.section_);
		}
		return sections;
	}

	/*!
		@brief
			Reverts the newest transaction and moves it to the redo stack; nothing while one is open.
		@param world
			World to write to
		@return
			The sections that were restored
		*/
	std::vector<glm::ivec3> History::undo(World& world)
	{
		if (open_ || undo_.empty())
			return {};

		Transaction t = std::move(undo_.back());
		undo_.pop_back();
		std::vector<glm::ivec3> sections = restore(world, t, true);
		redo_.push_back(std::move(t));
		return sections;
	}

	/*!
		@brief
			Re-applies the newest undone transaction and moves it back to the undo stack.
		@param world
			World to write to
		@return
			The sections that were restored
		*/
	std::vector<glm::ivec3> History::redo(World& world)
	{
		if (open_ || redo_.empty())
			return {};

		Transaction t = std::move(redo_.back());
		redo_.pop_back();
		std::vector<glm::ivec3> sections = restore(world, t, false);
		undo_.push_back(std::move(t));
		return sections;
	}

	/*!
		@brief
			Forgets every transaction, the open one included.
		*/
	void History::clear()
	{
		undo_.clear();
		redo_.clear();
		current_ = Transaction{};
		open_ = false;
		bytes_ = 0;
	}

	/*!
		@brief
			Drops the oldest transactions while over budget, always keeping the newest.
		*/
	void History::evict()
	{
		// The newest transaction always stays, however large: losing the
		// stroke that was just made would be worse than going over budget.
		while (bytes_ > budget_ && undo_.size() > 1)
		{
			bytes_ -= undo_.front().bytes_;
			undo_.pop_front();
		}
	}
}
