/*************************************************************************************************************
\file       plane_mesher.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      PlaneMesher: greedy rectangle merging of faces over one 32x32 plane, with rectangles capped
            at 16x16 to fit the quad format.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Voxel
#include "../types/voxel_types.hpp"

// stds
#include <algorithm>
#include <cstdint>
#include <cstring>


namespace voxel
{
	/**
	 * Greedy rectangle merging over one 32x32 plane of faces.
	 *
	 * Fill cells with a non-zero key (material, AO, anything that must
	 * match for two faces to share a quad), then emit(). Rectangles are
	 * capped at 16x16 so their sizes fit the quad format's 4-bit fields.
	 *
	 * A plain two-pass greedy scan rather than a streaming state machine:
	 * the streaming form only pays off when the input cannot be held, and
	 * a 32x32 plane is 8 KB.
	 *
	 */
	class PlaneMesher
	{
	public:

		static constexpr int kSize = kSectionSize;
		static constexpr int kMaxRun = Quad::kMaxSize;

		/*!
			@brief
				Empties every cell of the plane.
			*/
		void clear()
		{
			std::memset(cells_, 0, sizeof(cells_));
			used_ = false;
		}

		/*!
			@brief
				Sets one cell's face key.
			@param u
				Column in the plane
			@param v
				Row in the plane
			@param key
				Non-zero key that must match for two faces to merge
			*/
		void put(int u, int v, std::uint64_t key)
		{
			cells_[v * kSize + u] = key;
			used_ = true;
		}

		/*!
			@brief
				Reports whether any cell was put since the last clear or emit.
			@return
				true when the plane holds faces
			*/
		bool used() const { return used_; }

		/*!
			@brief
				Emits maximal rectangles, consuming the cells.
			@param fn
				Called as fn(u, v, length, width, key), with (u, v) the minimum
				corner, length along u and width along v
			*/
		template <typename Fn>
		void emit(Fn&& fn)
		{
			if (!used_)
				return;

			for (int v = 0; v < kSize; ++v)
			{
				for (int u = 0; u < kSize;)
				{
					const std::uint64_t key = cells_[v * kSize + u];
					if (key == 0)
					{
						++u;
						continue;
					}

					// Extend along u.
					int length = 1;
					while (length < kMaxRun && u + length < kSize && cells_[v * kSize + u + length] == key)
						++length;

					// Extend along v while the whole run matches.
					int width = 1;
					while (width < kMaxRun && v + width < kSize)
					{
						const std::uint64_t* row = &cells_[(v + width) * kSize + u];
						bool same = true;
						for (int i = 0; i < length; ++i)
						{
							if (row[i] != key)
							{
								same = false;
								break;
							}
						}
						if (!same)
							break;
						++width;
					}

					for (int dv = 0; dv < width; ++dv)
						std::memset(&cells_[(v + dv) * kSize + u], 0, sizeof(std::uint64_t) * length);

					fn(u, v, length, width, key);
					u += length;
				}
			}

			used_ = false;
		}

	private:

		std::uint64_t	cells_[kSize * kSize]{};
		bool			used_{ false };
	};
}
