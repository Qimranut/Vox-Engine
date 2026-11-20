/*************************************************************************************************************
\file       section.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      voxel::Section, one 32^3 block of materials that is either uniform or dense, and sectionPool(),
            the block pool every dense section's bytes come from.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Voxel
#include "../types/voxel_types.hpp"

// Memory
#include "../../util/memory/block_pool.hpp"

// stds
#include <cstdint>
#include <memory>


namespace voxel
{
	/*!
		@brief
			Where every dense section's 32 KiB of materials comes from: one pool of
			equal blocks, in 2 MiB slabs.

			Sections turn dense and collapse again all the time - the generator
			fills one and finds it uniform, a brush opens one, streaming brings a
			cell in and drops another - and the pool makes that a free-list pop
			instead of a trip to the heap, keeps the blocks together in memory,
			and knows how many bytes of voxels the process holds, which is the
			number a streaming budget is set against.

			Deliberately never destroyed: sections outlive any one system (the
			Editor's undo copies, a tool's own World), and a pool that went first
			would take their blocks with it. System::shutdown trims it instead.
			Thread-safe; workers allocate from it while they fill and mip.
		@return
			The process-wide section pool
		*/
	util::memory::BlockPool& sectionPool();

	/** Returns a dense section's block to sectionPool(). Stateless, so a Section stays 16 bytes. */
	struct SectionBlockDeleter
	{
		/*!
			@brief
				Returns a block to sectionPool().
			@param block
				The block to release
			*/
		void operator()(std::uint8_t* block) const noexcept;
	};

	using SectionBlock = std::unique_ptr<std::uint8_t[], SectionBlockDeleter>;

	/**
	 * 32^3 materials, either one value for the whole block (Uniform) or a
	 * dense byte array. Terrain is mostly uniform air above and uniform
	 * stone below, so this elision is what keeps a 1024x256x1024 world at
	 * tens of MB instead of 256 MB.
	 *
	 * Plain bytes rather than a palette: the mesher reads voxels in a tight
	 * loop and wants one indexed load. Palettes are a file-format concern.
	 *
	 */
	class Section
	{
	public:

		enum class Kind : std::uint8_t { Uniform, Dense };

		Section() = default;

		/*!
			@brief
				A uniform section of one material.
			@param m
				The material
			@return
				The section
			*/
		static Section uniform(MaterialId m)
		{
			Section s;
			s.uniform_ = m;
			return s;
		}

		/*!
			@brief
				A zero-filled (all air) dense section.
			@return
				The section
			*/
		static Section dense();

		Section(Section&&) noexcept = default;
		Section& operator=(Section&&) noexcept = default;
		Section(const Section&) = delete;
		Section& operator=(const Section&) = delete;

		/*!
			@brief
				Whether the section is uniform or dense.
			@return
				Its kind
			*/
		Kind kind() const { return dense_ ? Kind::Dense : Kind::Uniform; }

		/*!
			@brief
				Whether the section is one value.
			@return
				true when uniform
			*/
		bool isUniform() const { return !dense_; }

		/*!
			@brief
				Whether the section is uniform air.
			@return
				true when uniform air
			*/
		bool isAir() const { return !dense_ && uniform_ == kAir; }

		/*!
			@brief
				The section's single material. Valid only when uniform.
			@return
				The uniform material
			*/
		MaterialId uniformMaterial() const { return uniform_; }

		/*!
			@brief
				Reads one voxel.
			@param x
				Voxel x inside the section
			@param y
				Voxel y inside the section
			@param z
				Voxel z inside the section
			@return
				Its material
			*/
		MaterialId get(int x, int y, int z) const
		{
			return dense_ ? dense_[voxelIndex(x, y, z)] : uniform_;
		}

		/*!
			@brief
				Reads one voxel by its voxelIndex().
			@param index
				The voxel index
			@return
				Its material
			*/
		MaterialId at(std::size_t index) const
		{
			return dense_ ? dense_[index] : uniform_;
		}

		/*!
			@brief
				Writes one voxel, promoting a uniform section to dense when the
				value differs.
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
		bool set(int x, int y, int z, MaterialId m);

		/*!
			@brief
				Replaces the whole section with one value.
			@param m
				The material
			*/
		void fill(MaterialId m);

		/*!
			@brief
				Promotes to dense (filled with the uniform value) if needed.
			@return
				The dense voxel bytes
			*/
		std::uint8_t* makeDense();

		/*!
			@brief
				Collapses a dense section whose voxels are all equal back to
				uniform.
			@return
				true if it collapsed
			*/
		bool tryCollapse();

		/*!
			@brief
				The dense voxel bytes.
			@return
				The bytes, or nullptr when uniform
			*/
		const std::uint8_t* data() const { return dense_.get(); }

		/*!
			@brief
				The dense voxel bytes, writable.
			@return
				The bytes, or nullptr when uniform
			*/
		std::uint8_t* data() { return dense_.get(); }

		/*!
			@brief
				Deep copy (used by the cooker so a save never blocks the world).
			@return
				The copy
			*/
		Section clone() const;

	private:

		MaterialId						uniform_	{ kAir };
		SectionBlock					dense_;
	};

	// A world holds one Section per 32^3 block, dense or not.
	static_assert(sizeof(Section) == 16, "Section grew: check SectionBlockDeleter is still stateless");
}
