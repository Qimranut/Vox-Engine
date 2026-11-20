/*************************************************************************************************************
\file       edit_history.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Voxel undo / redo: SectionSnapshot (one level-0 section, compressed with the map codec) and
            History, which records a transaction's sections before and after and puts them back.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Voxel
#include "../types/voxel_types.hpp"
#include "../world/world.hpp"

// GLM
#include <glm/glm.hpp>

// stds
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <utility>
#include <vector>


namespace voxel::edit
{
	/**
	 * One level-0 section as it was at some moment, held compactly: a
	 * uniform section is one byte, a dense one goes through the map file's
	 * section codec (a palette plus bit-packed or run-length indices), which
	 * takes terrain from 32 KB to a few KB or less.
	 *
	 */
	class SectionSnapshot
	{
	public:

		/*!
			@brief
				Captures a section: its material if uniform, otherwise its voxels encoded with the map codec.
			@param section
				Section to capture
			@return
				The snapshot
			*/
		static SectionSnapshot capture(const Section& section);

		/*!
			@brief
				Rebuilds the section the snapshot was taken of.
			@return
				The restored section (collapsed to uniform when possible)
			*/
		Section restore() const;

		/*!
			@brief
				Memory the snapshot holds.
			@return
				Object size plus the encoded payload, in bytes
			*/
		std::size_t bytes() const { return sizeof(*this) + payload_.size(); }

	private:

		bool						uniform_	{ true };
		MaterialId					material_	{ kAir };
		std::uint8_t				codec_		{ 0 };
		std::vector<unsigned char>	payload_;
	};

	/**
	 * Undo and redo for voxel edits, a transaction at a time: one brush
	 * stroke, one Transform, however many writes it took. A transaction
	 * keeps each section it touched as it was before the first change and
	 * as it was when the transaction closed, so undoing is putting sections
	 * back - independent of what the edits were.
	 *
	 * Knows nothing about meshes, bodies or threads. Every call that takes a
	 * World writes to it, so it belongs to whoever owns the world's write
	 * window (voxel::System), and the sections it returns are what that
	 * owner has to re-mip and remesh.
	 *
	 */
	class History
	{
	public:

		using Id = std::uint64_t;

		/*!
			@brief
				Creates an empty history.
			@param budgetBytes
				Snapshot memory above which the oldest transactions are evicted
			*/
		explicit History(std::size_t budgetBytes = std::size_t{ 256 } << 20);

		/**
		 * Where a snapshot goes when its section is not resident (a streamed world
		 * evicted the cell since the edit). Undo, redo and rollback hand it over
		 * instead of writing it; without a sink it is dropped.
		 *
		 */
		using Offline = std::function<void(const glm::ivec3& section, const SectionSnapshot& snapshot)>;

		/*!
			@brief
				Sets where snapshots of non-resident sections go during undo, redo and rollback.
			@param sink
				Receiver of offline snapshots; empty drops them
			*/
		void setOffline(Offline sink) { offline_ = std::move(sink); }

		/*!
			@brief
				Opens a transaction.

				One that is already open stays open and keeps its id: a stroke is one
				transaction however it is driven.
			@param id
				Nonzero and chosen by the caller, who may have promised it to
				someone before the transaction could open
			@param label
				Name shown for the transaction (e.g. in undo menus)
			*/
		void begin(Id id, std::string label);

		/*!
			@brief
				Whether a transaction is open.
			@return
				True while one is open
			*/
		bool isOpen() const { return open_; }

		/*!
			@brief
				Id of the open transaction, 0 if none.
			@return
				The open transaction's id
			*/
		Id openId() const { return open_ ? current_.id_ : 0; }

		/*!
			@brief
				Hand to edit::write() as its BeforeChange.

				Keeps the first state seen of each section while a transaction is
				open; a no-op otherwise.
			@param section
				Level-0 section coordinate
			@param before
				The section before the change
			*/
		void touch(const glm::ivec3& section, const Section& before);

		/*!
			@brief
				Closes the open transaction, recording how its sections look now.

				One that touched nothing is dropped. Clears the redo stack and evicts
				the oldest transactions once over budget.
			@param world
				World to read the after-state from
			@return
				Whether a transaction was recorded
			*/
		bool commit(const World& world);

		/*!
			@brief
				Puts back every section the open transaction touched and forgets them; it stays open.

				This is how a live preview (the Transform tool) re-applies itself
				without piling up changes.
			@param world
				World to write to
			@return
				The sections that were restored
			*/
		std::vector<glm::ivec3> rollbackOpen(World& world);

		/*!
			@brief
				Whether there is a transaction to undo.
			@return
				True when the undo stack is not empty
			*/
		bool canUndo() const { return !undo_.empty(); }

		/*!
			@brief
				Whether there is a transaction to redo.
			@return
				True when the redo stack is not empty
			*/
		bool canRedo() const { return !redo_.empty(); }

		/*!
			@brief
				Whether a transaction is still there to be undone or redone.
			@param id
				Transaction id
			@return
				True when it is on the undo or redo stack
			*/
		bool contains(Id id) const;

		/*!
			@brief
				Id of the transaction undo() would revert, 0 if none.
			@return
				The id
			*/
		Id undoId() const { return undo_.empty() ? 0 : undo_.back().id_; }

		/*!
			@brief
				Id of the transaction redo() would re-apply, 0 if none.
			@return
				The id
			*/
		Id redoId() const { return redo_.empty() ? 0 : redo_.back().id_; }

		/*!
			@brief
				Label of the transaction undo() would revert.
			@return
				The label, empty if none
			*/
		const std::string& undoLabel() const;

		/*!
			@brief
				Label of the transaction redo() would re-apply.
			@return
				The label, empty if none
			*/
		const std::string& redoLabel() const;

		/*!
			@brief
				Reverts the newest transaction and moves it to the redo stack; nothing while one is open.
			@param world
				World to write to
			@return
				The sections that were restored; empty if there was nothing to undo
			*/
		std::vector<glm::ivec3> undo(World& world);

		/*!
			@brief
				Re-applies the newest undone transaction; nothing while one is open.
			@param world
				World to write to
			@return
				The sections that were restored; empty if there was nothing to redo
			*/
		std::vector<glm::ivec3> redo(World& world);

		/*!
			@brief
				Forgets everything, the open transaction included: the world was replaced.
			*/
		void clear();

		/*!
			@brief
				Memory held by recorded transactions.
			@return
				Bytes
			*/
		std::size_t bytes() const { return bytes_; }

		/*!
			@brief
				The memory budget.
			@return
				Bytes
			*/
		std::size_t budget() const { return budget_; }

		/*!
			@brief
				Number of transactions that can be undone.
			@return
				The undo stack size
			*/
		std::size_t undoCount() const { return undo_.size(); }

	private:

		struct Entry
		{
			glm::ivec3		section_;
			SectionSnapshot	before_;
			SectionSnapshot	after_;
		};

		struct Transaction
		{
			Id					id_		{ 0 };
			std::string			label_;
			std::vector<Entry>	entries_;
			std::size_t			bytes_	{ 0 };
		};

		/*!
			@brief
				Writes a transaction's before or after snapshots back into the world.

				Non-resident sections go to the offline sink instead and are not
				reported.
			@param world
				World to write to
			@param t
				Transaction to restore
			@param before
				True for the before-state, false for the after-state
			@return
				The sections written
			*/
		std::vector<glm::ivec3> restore(World& world, const Transaction& t, bool before) const;

		/*!
			@brief
				Drops the oldest transactions while over budget, always keeping the newest.
			*/
		void evict();

		std::deque<Transaction>	undo_;		// oldest first
		std::vector<Transaction> redo_;
		Transaction				current_;
		bool					open_		{ false };
		std::size_t				bytes_		{ 0 };
		std::size_t				budget_;
		Offline					offline_;
	};
}
