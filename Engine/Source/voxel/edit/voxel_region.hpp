/*************************************************************************************************************
\file       voxel_region.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      VoxelBox and VoxelRegion, a dense copy of a box of level-0 voxels, with read() and write()
            to move one in and out of the world and report which sections must remesh.

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
#include <functional>
#include <vector>


namespace voxel::edit
{
	/**
	 * A box of voxels, min inclusive and max exclusive, in level-0 voxel
	 * coordinates. May reach outside the world; reads there are air and
	 * writes there are dropped.
	 *
	 */
	struct VoxelBox
	{
		glm::ivec3	min_	{ 0 };
		glm::ivec3	max_	{ 0 };

		/*!
			@brief
				Extent of the box per axis, never negative.
			@return
				max - min, clamped at 0
			*/
		glm::ivec3 size() const { return glm::max(max_ - min_, glm::ivec3{ 0 }); }

		/*!
			@brief
				Whether the box holds no voxels.
			@return
				True when any axis has max <= min
			*/
		bool empty() const { return max_.x <= min_.x || max_.y <= min_.y || max_.z <= min_.z; }

		/*!
			@brief
				Number of voxels in the box.
			@return
				The product of the sizes
			*/
		std::size_t volume() const
		{
			const glm::ivec3 s = size();
			return static_cast<std::size_t>(s.x) * static_cast<std::size_t>(s.y) * static_cast<std::size_t>(s.z);
		}

		/*!
			@brief
				Whether a voxel lies in the box.
			@param x
				Voxel x
			@param y
				Voxel y
			@param z
				Voxel z
			@return
				True when min <= p < max on every axis
			*/
		bool contains(int x, int y, int z) const
		{
			return x >= min_.x && y >= min_.y && z >= min_.z && x < max_.x && y < max_.y && z < max_.z;
		}

		/*!
			@brief
				The box grown by the same amount on every side.
			@param by
				Voxels to add on each side
			@return
				The expanded box
			*/
		VoxelBox expanded(int by) const { return { min_ - glm::ivec3{ by }, max_ + glm::ivec3{ by } }; }

		/*!
			@brief
				The intersection with another box.
			@param to
				Box to clip against
			@return
				The clipped box (may be empty)
			*/
		VoxelBox clipped(const VoxelBox& to) const { return { glm::max(min_, to.min_), glm::min(max_, to.max_) }; }

		/*!
			@brief
				The smallest box holding both; an empty box is ignored.
			@param with
				Box to unite with
			@return
				The united box
			*/
		VoxelBox united(const VoxelBox& with) const
		{
			if (empty()) return with;
			if (with.empty()) return *this;
			return { glm::min(min_, with.min_), glm::max(max_, with.max_) };
		}

		/*!
			@brief
				The voxels a world actually stores.
			@param dims
				The world's dimensions
			@return
				Box from 0 to the logical voxel extent
			*/
		static VoxelBox ofWorld(const WorldDims& dims) { return { glm::ivec3{ 0 }, dims.voxels_ }; }

		bool operator==(const VoxelBox&) const = default;
	};

	/**
	 * A dense copy of a box of the world: what every brush op and the
	 * Transform tool work on. Read a region, change it, write it back; the
	 * write finds out which sections really changed.
	 *
	 * Laid out like a section - Y-major, then Z, then X - so an X run is
	 * contiguous and moves in and out of the world with one memcpy.
	 *
	 */
	class VoxelRegion
	{
	public:

		VoxelRegion() = default;

		/*!
			@brief
				Creates a region over a box, every voxel set to fill.
			@param box
				Voxels the region covers
			@param fill
				Initial material
			*/
		explicit VoxelRegion(const VoxelBox& box, MaterialId fill = kAir)
		{
			reset(box, fill);
		}

		/*!
			@brief
				Resizes the region to a box and sets every voxel to fill.
			@param box
				Voxels the region covers
			@param fill
				Initial material
			*/
		void reset(const VoxelBox& box, MaterialId fill = kAir)
		{
			box_ = box;
			size_ = box.size();
			values_.assign(box.volume(), fill);
		}

		/*!
			@brief
				The box the region covers.
			@return
				The box
			*/
		const VoxelBox& box() const { return box_; }

		/*!
			@brief
				The region's extent per axis.
			@return
				The size
			*/
		const glm::ivec3& size() const { return size_; }

		/*!
			@brief
				Whether a voxel lies in the region.
			@param x
				Voxel x
			@param y
				Voxel y
			@param z
				Voxel z
			@return
				True when inside box()
			*/
		bool contains(int x, int y, int z) const { return box_.contains(x, y, z); }

		/*!
			@brief
				Index of a voxel given in world voxel coordinates; it must be inside box().
			@param x
				Voxel x
			@param y
				Voxel y
			@param z
				Voxel z
			@return
				Index into the value array
			*/
		std::size_t index(int x, int y, int z) const
		{
			return (static_cast<std::size_t>(y - box_.min_.y) * static_cast<std::size_t>(size_.z)
				+ static_cast<std::size_t>(z - box_.min_.z)) * static_cast<std::size_t>(size_.x)
				+ static_cast<std::size_t>(x - box_.min_.x);
		}

		/*!
			@brief
				Reads a voxel; air outside the region.
			@param x
				Voxel x
			@param y
				Voxel y
			@param z
				Voxel z
			@return
				The material
			*/
		MaterialId get(int x, int y, int z) const
		{
			return contains(x, y, z) ? values_[index(x, y, z)] : kAir;
		}

		/*!
			@brief
				Writes a voxel; dropped outside the region.
			@param x
				Voxel x
			@param y
				Voxel y
			@param z
				Voxel z
			@param m
				Material to write
			*/
		void set(int x, int y, int z, MaterialId m)
		{
			if (contains(x, y, z))
				values_[index(x, y, z)] = m;
		}

		/*!
			@brief
				Unchecked reference to a voxel inside the region.

				Through data(): the ops call these per voxel, and a checked
				operator[] is most of a Debug build's cost there.
			@param x
				Voxel x
			@param y
				Voxel y
			@param z
				Voxel z
			@return
				Reference to the voxel's material
			*/
		MaterialId& at(int x, int y, int z) { return values_.data()[index(x, y, z)]; }

		/*!
			@brief
				Unchecked const reference to a voxel inside the region.
			@param x
				Voxel x
			@param y
				Voxel y
			@param z
				Voxel z
			@return
				Reference to the voxel's material
			*/
		const MaterialId& at(int x, int y, int z) const { return values_.data()[index(x, y, z)]; }

		/*!
			@brief
				The raw voxel array (Y-major, then Z, then X).
			@return
				Pointer to the first voxel
			*/
		MaterialId* data() { return values_.data(); }

		/*!
			@brief
				The raw voxel array (Y-major, then Z, then X), read-only.
			@return
				Pointer to the first voxel
			*/
		const MaterialId* data() const { return values_.data(); }

		/*!
			@brief
				Number of voxels held.
			@return
				The voxel count
			*/
		std::size_t volume() const { return values_.size(); }

	private:

		VoxelBox				box_;
		glm::ivec3				size_	{ 0 };
		std::vector<MaterialId>	values_;
	};

	/**
	 * What write() reports. Coordinates are level-0 sections.
	 *
	 */
	struct WriteResult
	{
		// Sections in which at least one voxel changed.
		std::vector<glm::ivec3>	changed_;

		// Face neighbours of those that must remesh although their own voxels
		// did not change: a voxel changed on the plane they share, and a
		// section's boundary faces depend on the voxel across the border.
		std::vector<glm::ivec3>	neighbours_;

		std::size_t				voxelsChanged_	{ 0 };
	};

	/** Called with a section's coordinate and contents just before write() first changes it. */
	using BeforeChange = std::function<void(const glm::ivec3& section, const Section& before)>;

	/*!
		@brief
			Copies box out of level 0 of the world.
		@param world
			World to read
		@param box
			Voxels to copy
		@param out
			Resized to box; the part of it outside the world reads as air
		*/
	void read(const World& world, const VoxelBox& box, VoxelRegion& out);

	/*!
		@brief
			Writes a region back into level 0, section by section.

			A section none of whose voxels differ is not touched at all (it stays
			Uniform if it was), and one left with a single material collapses back
			to Uniform. Only the caller that owns the world's write window may call
			this - see voxel::System. Does not re-mip or remesh; the result says
			what has to.
		@param world
			World to write to
		@param region
			Voxels to write
		@param beforeChange
			Optional; how the edit history snapshots a section the first time it changes
		@return
			The changed sections, the neighbours that must remesh and the voxel count
		*/
	WriteResult write(World& world, const VoxelRegion& region, const BeforeChange& beforeChange = {});
}
