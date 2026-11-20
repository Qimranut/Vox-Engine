/*************************************************************************************************************
\file       mipper.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      mipSection(): builds a LoD parent section from its eight children. Optimised in Debug builds too,
            because every edit re-mips the ancestors of the sections it touched on the main thread.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

// Optimised in Debug builds too: every edit re-mips the ancestors of the
// sections it touched on the main thread, 32768 voxels a section.
#if defined(_MSC_VER) && defined(_DEBUG)
#pragma runtime_checks("", off)
#pragma optimize("gt", on)
#endif

#include "mipper.hpp"


namespace voxel
{
	/*!
		@brief
			Builds one parent section from its eight children; all-uniform children
			take a fast path to a uniform parent.
		@param children
			Indexed by childIndex(cx, cy, cz); nullptr for children outside the world
		@param materials
			The table the LoD priorities come from
		@return
			The parent section, collapsed to uniform when possible
		*/
	Section mipSection(const Section* const children[8], const MaterialTable& materials)
	{
		// Fast path: eight uniform (or absent) children collapse to one value.
		{
			bool allUniform = true;
			MaterialId uniform[8];
			for (int k = 0; k < 8; ++k)
			{
				if (children[k] == nullptr)
				{
					uniform[k] = kAir;
				}
				else if (children[k]->isUniform())
				{
					uniform[k] = children[k]->uniformMaterial();
				}
				else
				{
					allUniform = false;
					break;
				}
			}

			if (allUniform)
				return Section::uniform(mip8(uniform, materials));
		}

		Section parent = Section::dense();
		std::uint8_t* out = parent.data();

		constexpr int kHalf = kSectionSize / 2;

		for (int cx = 0; cx < 2; ++cx)
		for (int cy = 0; cy < 2; ++cy)
		for (int cz = 0; cz < 2; ++cz)
		{
			const Section* child = children[childIndex(cx, cy, cz)];

			if (child == nullptr || child->isAir())
				continue;	// parent is zero-filled already

			if (child->isUniform())
			{
				const MaterialId m = child->uniformMaterial();
				for (int y = 0; y < kHalf; ++y)
				for (int z = 0; z < kHalf; ++z)
				for (int x = 0; x < kHalf; ++x)
					out[voxelIndex(cx * kHalf + x, cy * kHalf + y, cz * kHalf + z)] = m;
				continue;
			}

			const std::uint8_t* in = child->data();

			for (int y = 0; y < kHalf; ++y)
			for (int z = 0; z < kHalf; ++z)
			for (int x = 0; x < kHalf; ++x)
			{
				MaterialId c[8];
				for (int dx = 0; dx < 2; ++dx)
				for (int dy = 0; dy < 2; ++dy)
				for (int dz = 0; dz < 2; ++dz)
					c[childIndex(dx, dy, dz)] = in[voxelIndex(2 * x + dx, 2 * y + dy, 2 * z + dz)];

				out[voxelIndex(cx * kHalf + x, cy * kHalf + y, cz * kHalf + z)] = mip8(c, materials);
			}
		}

		parent.tryCollapse();
		return parent;
	}
}
