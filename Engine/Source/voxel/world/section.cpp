/*************************************************************************************************************
\file       section.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Section storage: the process-wide section block pool and the dense / uniform transitions
            (promote, write, collapse, clone).

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "section.hpp"

// stds
#include <cstring>


namespace voxel
{
	namespace
	{
		// 64 sections to a slab: 2 MiB, a size the system hands out and takes
		// back cheaply, and small enough that a half-used slab is not a waste
		// worth worrying about.
		constexpr std::size_t kBlocksPerSlab = 64;

		/*!
			@brief
				Takes an uninitialised block from sectionPool().
			@return
				The block
			*/
		SectionBlock newBlock()
		{
			return SectionBlock(static_cast<std::uint8_t*>(sectionPool().allocate()));
		}
	}

	/*!
		@brief
			The process-wide section pool, created on first use and leaked on purpose.
		@return
			The pool
		*/
	util::memory::BlockPool& sectionPool()
	{
		// Leaked on purpose; see the declaration.
		static util::memory::BlockPool* const pool =
			new util::memory::BlockPool(kSectionVolume, kBlocksPerSlab, "Voxel sections");

		return *pool;
	}

	/*!
		@brief
			Returns a block to sectionPool().
		@param block
			The block to release
		*/
	void SectionBlockDeleter::operator()(std::uint8_t* block) const noexcept
	{
		sectionPool().release(block);
	}

	/*!
		@brief
			A zero-filled (all air) dense section.
		@return
			The section
		*/
	Section Section::dense()
	{
		Section s;
		s.dense_ = newBlock();
		std::memset(s.dense_.get(), 0, kSectionVolume);
		return s;
	}

	/*!
		@brief
			Writes one voxel, promoting a uniform section to dense when the value differs.
		@param x
			Voxel x inside the section
		@param y
			Voxel y inside the section
		@param z
			Voxel z inside the section
		@param m
			The material to write
		@return
			true if the voxel changed
		*/
	bool Section::set(int x, int y, int z, MaterialId m)
	{
		if (!dense_)
		{
			if (uniform_ == m)
				return false;
			makeDense();
		}

		std::uint8_t& slot = dense_[voxelIndex(x, y, z)];
		if (slot == m)
			return false;

		slot = m;
		return true;
	}

	/*!
		@brief
			Replaces the whole section with one value, releasing any dense block.
		@param m
			The material
		*/
	void Section::fill(MaterialId m)
	{
		dense_.reset();
		uniform_ = m;
	}

	/*!
		@brief
			Promotes to dense (filled with the uniform value) if needed.
		@return
			The dense voxel bytes
		*/
	std::uint8_t* Section::makeDense()
	{
		if (!dense_)
		{
			dense_ = newBlock();
			std::memset(dense_.get(), uniform_, kSectionVolume);
		}
		return dense_.get();
	}

	/*!
		@brief
			Collapses a dense section whose voxels are all equal back to uniform.
		@return
			true if it collapsed
		*/
	bool Section::tryCollapse()
	{
		if (!dense_)
			return false;

		const std::uint8_t first = dense_[0];
		for (int i = 1; i < kSectionVolume; ++i)
		{
			if (dense_[i] != first)
				return false;
		}

		dense_.reset();
		uniform_ = first;
		return true;
	}

	/*!
		@brief
			Deep copy, with its own pool block when dense.
		@return
			The copy
		*/
	Section Section::clone() const
	{
		Section s;
		s.uniform_ = uniform_;
		if (dense_)
		{
			s.dense_ = newBlock();
			std::memcpy(s.dense_.get(), dense_.get(), kSectionVolume);
		}
		return s;
	}
}
