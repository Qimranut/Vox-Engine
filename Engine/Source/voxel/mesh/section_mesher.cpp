/*************************************************************************************************************
\file       section_mesher.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Meshes a voxel section: row-mask XOR face finding, optional corner ambient occlusion, greedy
            plane merging and packing into direction-bucketed quads.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "section_mesher.hpp"

// Voxel
#include "../world/world.hpp"
#include "plane_mesher.hpp"

// Profiling
#include "../../profile/profiler.hpp"

// stds
#include <algorithm>
#include <array>
#include <bit>
#include <cstring>


namespace voxel
{
	/*!
		@brief
			Gathers a section and its six same-level face neighbours; levels above 0 seal borders.
		@param world
			World to read from
		@param level
			LoD level
		@param c
			Section coordinate at that level
		@return
			The meshing input
		*/
	MeshInput gatherMeshInput(const World& world, int level, const glm::ivec3& c)
	{
		VOX_PROFILE_FUNCTION_C(profile::colour::kVoxel);

		MeshInput in;
		in.self_ = world.sectionOrNull(level, c.x, c.y, c.z);
		for (int f = 0; f < FaceCount; ++f)
		{
			const glm::ivec3 n = c + faceNormal(f);
			in.neighbours_[f] = world.sectionOrNull(level, n.x, n.y, n.z);
		}
		in.sealBorders_ = level > 0;
		return in;
	}

	namespace
	{
		constexpr int S = kSectionSize;

		// Solid-ness of any voxel in the section or one step outside it.
		// Outside on two axes at once (edge and corner diagonals across a
		// section border) is treated as air.
		struct Neighbourhood
		{
			const MeshInput&	in_;
			const bool*			opaque_;	// 256-entry LUT

			/*!
				@brief
					Tests whether a voxel in the section, or one step outside it, is opaque.
				@param x
					Voxel x, -1..32
				@param y
					Voxel y, -1..32
				@param z
					Voxel z, -1..32
				@return
					true when opaque; false for air, missing neighbours and diagonal outside cells
				*/
			bool solid(int x, int y, int z) const
			{
				const Section* s = in_.self_;
				int outside = 0;
				if (x < 0 || x >= S) ++outside;
				if (y < 0 || y >= S) ++outside;
				if (z < 0 || z >= S) ++outside;

				if (outside == 0)
					return s && opaque_[s->get(x, y, z)];
				if (outside > 1)
					return false;

				if (x < 0)  { s = in_.neighbours_[FaceNegX]; x += S; }
				else if (x >= S) { s = in_.neighbours_[FacePosX]; x -= S; }
				else if (y < 0)  { s = in_.neighbours_[FaceNegY]; y += S; }
				else if (y >= S) { s = in_.neighbours_[FacePosY]; y -= S; }
				else if (z < 0)  { s = in_.neighbours_[FaceNegZ]; z += S; }
				else             { s = in_.neighbours_[FacePosZ]; z -= S; }

				return s && opaque_[s->get(x, y, z)];
			}
		};

		/*!
			@brief
				Computes classic corner AO for the four corners of a face: 0 (fully occluded) .. 3 (open).

				The corner cell is ignored when both sides are solid, which
				avoids the "light leak" at inside corners.
			@param n
				Opacity lookup around the section
			@param front
				The air voxel in front of the face
			@param du
				Unit step along the face's u axis
			@param dv
				Unit step along the face's v axis
			@return
				Four 2-bit AO levels packed into a byte
			*/
		std::uint8_t cornerAo(const Neighbourhood& n, const glm::ivec3& front, const glm::ivec3& du, const glm::ivec3& dv)
		{
			std::uint8_t ao = 0;
			for (int k = 0; k < 4; ++k)
			{
				const glm::ivec3 su = (k & 2) ? du : -du;
				const glm::ivec3 sv = (k & 1) ? dv : -dv;

				const int s1 = n.solid(front.x + su.x, front.y + su.y, front.z + su.z) ? 1 : 0;
				const int s2 = n.solid(front.x + sv.x, front.y + sv.y, front.z + sv.z) ? 1 : 0;
				const int c = n.solid(front.x + su.x + sv.x, front.y + su.y + sv.y, front.z + su.z + sv.z) ? 1 : 0;

				const int level = (s1 && s2) ? 0 : 3 - (s1 + s2 + c);
				ao |= static_cast<std::uint8_t>(level << (2 * k));
			}
			return ao;
		}

		struct Emitter
		{
			SectionMesh&				mesh_;
			std::vector<Quad>			buckets_[FaceCount];
			glm::ivec3					mn_{ S, S, S };
			glm::ivec3					mx_{ -1, -1, -1 };

			/*!
				@brief
					Packs one merged rectangle into a quad in its face bucket and grows the bounds.
				@param face
					Face direction
				@param u
					Minimum corner along the plane's u axis
				@param v
					Minimum corner along the plane's v axis
				@param length
					Extent along u
				@param width
					Extent along v
				@param key
					Plane cell key: material, AO and layer depth
				*/
			void emit(int face, int u, int v, int length, int width, std::uint64_t key)
			{
				const int axis = faceAxis(face);
				const int d = static_cast<int>(key >> 48);
				const std::uint16_t material = static_cast<std::uint16_t>(key & 0xFFFFu);
				const std::uint8_t ao = static_cast<std::uint8_t>((key >> 16) & 0xFFu);

				const glm::ivec3 p = unswizzle(axis, u, v, d);
				buckets_[face].push_back(Quad::pack(static_cast<std::uint8_t>(face), length, width, p.x, p.y, p.z, material, ao));

				const glm::ivec3 far = unswizzle(axis, u + length - 1, v + width - 1, d);
				mn_ = glm::min(mn_, p);
				mx_ = glm::max(mx_, far);
			}

			/*!
				@brief
					Concatenates the buckets into the mesh and writes its counts and packed AABB.
				*/
			void finish()
			{
				std::size_t total = 0;
				for (int f = 0; f < FaceCount; ++f)
				{
					mesh_.counts_[f] = static_cast<std::uint16_t>(buckets_[f].size());
					total += buckets_[f].size();
				}

				mesh_.quads_.reserve(total);
				for (int f = 0; f < FaceCount; ++f)
					mesh_.quads_.insert(mesh_.quads_.end(), buckets_[f].begin(), buckets_[f].end());

				if (total == 0)
				{
					mesh_.aabb_ = 0;
					return;
				}

				mesh_.aabb_ = static_cast<std::uint32_t>(mn_.x)
					| static_cast<std::uint32_t>(mn_.y) << 5
					| static_cast<std::uint32_t>(mn_.z) << 10
					| static_cast<std::uint32_t>(mx_.x - mn_.x) << 15
					| static_cast<std::uint32_t>(mx_.y - mn_.y) << 20
					| static_cast<std::uint32_t>(mx_.z - mn_.z) << 25;
			}
		};

		/*!
			@brief
				Builds the key packed into a plane cell: material | ao << 16 | d << 48.

				d rides along so one plane can hold faces from one layer only,
				and the emitter can recover it without extra state.
			@param material
				Face material
			@param ao
				Packed corner AO
			@param d
				Layer depth along the face axis
			@return
				The cell key
			*/
		inline std::uint64_t cellKey(MaterialId material, std::uint8_t ao, int d)
		{
			return static_cast<std::uint64_t>(material)
				| static_cast<std::uint64_t>(ao) << 16
				| static_cast<std::uint64_t>(d) << 48;
		}
	}

	/*!
		@brief
			Turns a section into greedy-merged quads, bucketed by face direction.
		@param in
			The section and its neighbours
		@param materials
			Material table, for opacity
		@param ao
			Bake corner ambient occlusion; off, every quad carries AO 0xFF
		@return
			The mesh; empty for uniform air or a fully enclosed section
		*/
	SectionMesh meshSection(const MeshInput& in, const MaterialTable& materials, bool ao)
	{
		VOX_PROFILE_FUNCTION_C(profile::colour::kVoxel);

		SectionMesh mesh;

		if (in.self_ == nullptr || in.self_->isAir())
			return mesh;

		bool opaqueLut[256];
		for (int i = 0; i < 256; ++i)
			opaqueLut[i] = materials.opaque(static_cast<MaterialId>(i));

		const Section& self = *in.self_;
		const Neighbourhood hood{ in, opaqueLut };

		// One 32-bit row per (y, z): bit x set when the voxel is opaque.
		std::array<std::uint32_t, S * S> opaque{};
		if (self.isUniform())
		{
			if (opaqueLut[self.uniformMaterial()])
				opaque.fill(0xFFFFFFFFu);
		}
		else
		{
			const std::uint8_t* data = self.data();
			for (int y = 0; y < S; ++y)
			for (int z = 0; z < S; ++z)
			{
				std::uint32_t row = 0;
				const std::uint8_t* r = data + voxelIndex(0, y, z);
				for (int x = 0; x < S; ++x)
					row |= static_cast<std::uint32_t>(opaqueLut[r[x]] ? 1u : 0u) << x;
				opaque[static_cast<std::size_t>(y * S + z)] = row;
			}
		}

		// Row of the neighbour's boundary plane facing us, as a mask over x
		// (Y/Z neighbours) or over z (X neighbours).
		const auto neighbourRow = [&](int face, int a, int b) -> std::uint32_t
		{
			const Section* n = in.neighbours_[face];
			if (n == nullptr || in.sealBorders_)
				return 0;
			if (n->isUniform())
				return opaqueLut[n->uniformMaterial()] ? 0xFFFFFFFFu : 0u;

			std::uint32_t row = 0;
			switch (face)
			{
			case FaceNegY: for (int x = 0; x < S; ++x) row |= (opaqueLut[n->get(x, S - 1, a)] ? 1u : 0u) << x; break;	// a = z
			case FacePosY: for (int x = 0; x < S; ++x) row |= (opaqueLut[n->get(x, 0, a)] ? 1u : 0u) << x; break;
			case FaceNegZ: for (int x = 0; x < S; ++x) row |= (opaqueLut[n->get(x, a, S - 1)] ? 1u : 0u) << x; break;	// a = y
			case FacePosZ: for (int x = 0; x < S; ++x) row |= (opaqueLut[n->get(x, a, 0)] ? 1u : 0u) << x; break;
			case FaceNegX: for (int z = 0; z < S; ++z) row |= (opaqueLut[n->get(S - 1, a, z)] ? 1u : 0u) << z; break;	// a = y
			default:       for (int z = 0; z < S; ++z) row |= (opaqueLut[n->get(0, a, z)] ? 1u : 0u) << z; break;
			}
			(void)b;
			return row;
		};

		Emitter out{ mesh };
		PlaneMesher planeNeg, planePos;
		planeNeg.clear();
		planePos.clear();

		const auto flush = [&](int axis)
		{
			planeNeg.emit([&](int u, int v, int l, int w, std::uint64_t key) { out.emit(axis << 1, u, v, l, w, key); });
			planePos.emit([&](int u, int v, int l, int w, std::uint64_t key) { out.emit((axis << 1) | 1, u, v, l, w, key); });
		};

		// Records a face owned by voxel p, facing `face`.
		const auto putFace = [&](int face, const glm::ivec3& p)
		{
			const int axis = faceAxis(face);
			const MaterialId m = self.get(p.x, p.y, p.z);

			std::uint8_t aoBits = 0xFF;
			if (ao)
			{
				const glm::ivec3 n = faceNormal(face);
				const glm::ivec3 du = unswizzle(axis, 1, 0, 0);
				const glm::ivec3 dv = unswizzle(axis, 0, 1, 0);
				const glm::ivec3 front = p + n;

				// A sealed border face looks into a solid neighbour; baking
				// that in would paint the crack filler black.
				if (!hood.solid(front.x, front.y, front.z))
					aoBits = cornerAo(hood, front, du, dv);
			}

			int u, v, d;
			switch (axis)
			{
			case 0:  u = p.x; v = p.z; d = p.y; break;
			case 1:  u = p.x; v = p.y; d = p.z; break;
			default: u = p.y; v = p.z; d = p.x; break;
			}

			(faceSide(face) ? planePos : planeNeg).put(u, v, cellKey(m, aoBits, d));
		};

		// ------------------------------ Y ------------------------------
		// Inner layers: faces between y and y+1.
		for (int y = 0; y < S - 1; ++y)
		{
			for (int z = 0; z < S; ++z)
			{
				const std::uint32_t cur = opaque[static_cast<std::size_t>(y * S + z)];
				std::uint32_t msk = cur ^ opaque[static_cast<std::size_t>((y + 1) * S + z)];
				if (msk == 0)
					continue;

				const std::uint32_t fwd = msk & cur;
				while (msk)
				{
					const int x = std::countr_zero(msk);
					msk &= msk - 1;

					if ((fwd >> x) & 1u)
						putFace(FacePosY, { x, y, z });		// solid at y, air above
					else
						putFace(FaceNegY, { x, y + 1, z });	// solid at y+1, air below
				}
			}
			flush(0);
		}
		// Outer: y = 0 faces -Y against the neighbour below, y = 31 faces +Y.
		for (int z = 0; z < S; ++z)
		{
			std::uint32_t below = opaque[static_cast<std::size_t>(0 * S + z)] & ~neighbourRow(FaceNegY, z, 0);
			while (below)
			{
				const int x = std::countr_zero(below);
				below &= below - 1;
				putFace(FaceNegY, { x, 0, z });
			}

			std::uint32_t above = opaque[static_cast<std::size_t>((S - 1) * S + z)] & ~neighbourRow(FacePosY, z, 0);
			while (above)
			{
				const int x = std::countr_zero(above);
				above &= above - 1;
				putFace(FacePosY, { x, S - 1, z });
			}
		}
		flush(0);

		// ------------------------------ Z ------------------------------
		for (int z = 0; z < S - 1; ++z)
		{
			for (int y = 0; y < S; ++y)
			{
				const std::uint32_t cur = opaque[static_cast<std::size_t>(y * S + z)];
				std::uint32_t msk = cur ^ opaque[static_cast<std::size_t>(y * S + z + 1)];
				if (msk == 0)
					continue;

				const std::uint32_t fwd = msk & cur;
				while (msk)
				{
					const int x = std::countr_zero(msk);
					msk &= msk - 1;

					if ((fwd >> x) & 1u)
						putFace(FacePosZ, { x, y, z });
					else
						putFace(FaceNegZ, { x, y, z + 1 });
				}
			}
			flush(1);
		}
		for (int y = 0; y < S; ++y)
		{
			std::uint32_t back = opaque[static_cast<std::size_t>(y * S + 0)] & ~neighbourRow(FaceNegZ, y, 0);
			while (back)
			{
				const int x = std::countr_zero(back);
				back &= back - 1;
				putFace(FaceNegZ, { x, y, 0 });
			}

			std::uint32_t front = opaque[static_cast<std::size_t>(y * S + S - 1)] & ~neighbourRow(FacePosZ, y, 0);
			while (front)
			{
				const int x = std::countr_zero(front);
				front &= front - 1;
				putFace(FacePosZ, { x, y, S - 1 });
			}
		}
		flush(1);

		// ------------------------------ X ------------------------------
		// The row runs along x, so adjacent voxels are adjacent bits.
		for (int x = 0; x < S - 1; ++x)
		{
			for (int y = 0; y < S; ++y)
			for (int z = 0; z < S; ++z)
			{
				const std::uint32_t pair = (opaque[static_cast<std::size_t>(y * S + z)] >> x) & 3u;
				if (pair == 1u)
					putFace(FacePosX, { x, y, z });
				else if (pair == 2u)
					putFace(FaceNegX, { x + 1, y, z });
			}
			flush(2);
		}
		for (int y = 0; y < S; ++y)
		{
			const std::uint32_t nNeg = neighbourRow(FaceNegX, y, 0);	// bit z
			const std::uint32_t nPos = neighbourRow(FacePosX, y, 0);
			for (int z = 0; z < S; ++z)
			{
				const std::uint32_t row = opaque[static_cast<std::size_t>(y * S + z)];
				if ((row & 1u) && !((nNeg >> z) & 1u))
					putFace(FaceNegX, { 0, y, z });
				if ((row >> (S - 1)) & 1u && !((nPos >> z) & 1u))
					putFace(FacePosX, { S - 1, y, z });
			}
		}
		flush(2);

		out.finish();
		return mesh;
	}
}
