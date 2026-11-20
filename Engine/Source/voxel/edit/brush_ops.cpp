/*************************************************************************************************************
\file       brush_ops.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      The brush op table and every op (Add, Subtract, Grow, Shrink, Smooth, Flatten, Paint, Replace,
            Erode, Fill) over a VoxelRegion, with the density field the gradual ops read.
            A stamp visits a few million voxels, so this file is optimised in Debug builds too; run-time
            checks cannot be combined with that, and both pragmas must precede the inline functions used.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#if defined(_MSC_VER) && defined(_DEBUG)
#pragma runtime_checks("", off)
#pragma optimize("gt", on)
#endif

#include "brush_ops.hpp"

// Voxel
#include "../gen/erosion.hpp"
#include "../gen/heightfield.hpp"

// Profiling
#include "../../profile/profiler.hpp"

// GLM
#include <glm/glm.hpp>

// stds
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>


namespace voxel::edit
{
	namespace
	{
		// How far the surface moves per stamp at full strength, as a share of
		// the way from "half the neighbourhood is solid" to "all of it is".
		constexpr float kSculptGain = 0.4f;

		// At zero strength Smooth still removes what stands this far from the
		// half-solid balance: lone spikes and pinholes, nothing else.
		constexpr float kSmoothMargin = 0.35f;

		// Ground the erosion pass sees around the brush, in voxels. Droplets
		// that leave the patch are lost, so the rim must not be the brush's edge.
		constexpr int kErodePad = 6;

		// The droplet pass scales heights to 0..1. Treating the patch as at
		// least this tall (in voxels) keeps a gentle slope from being cut as
		// deeply as a cliff - the same floor the generator's recipes set.
		constexpr float kErodeMinRelief = 200.f;

		/*!
			@brief
				Radius of the neighbourhood a gradual op averages over: a quarter of the brush, 2..8.

				It grows with the brush, so a large brush smooths large features.
			@param stamp
				The brush application
			@return
				Filter radius in voxels
			*/
		int filterRadius(const Stamp& stamp)
		{
			return std::clamp(static_cast<int>(stamp.radius() * 0.25f), 2, 8);
		}

		/**
		 * Share of solid voxels in the cube of (2r + 1)^3 around each voxel:
		 * a binary grid's stand-in for a smooth density field. 0.5 is the
		 * surface. Three sliding-window passes, so the cost does not depend on r.
		 *
		 * A snapshot: it does not follow changes made to the region afterwards,
		 * which is what keeps an op from feeding on its own output.
		 *
		 */
		class Density
		{
		public:

			/*!
				@brief
					Builds the box-sum density of the region over the wanted box with three sliding-window passes.
				@param region
					Voxels to read
				@param wanted
					Box to compute density for; clipped to the region
				@param radius
					Half width of the averaging cube, in voxels
				*/
			Density(const VoxelRegion& region, const VoxelBox& wanted, int radius)
				: box_(wanted.clipped(region.box()))
				, size_(box_.size())
				, scale_(1.f / static_cast<float>((2 * radius + 1) * (2 * radius + 1) * (2 * radius + 1)))
			{
				VOX_PROFILE_SCOPE_C("Brush density", profile::colour::kVoxel);

				if (box_.empty())
					return;

				std::vector<std::uint16_t> a(box_.volume()), b(box_.volume());

				std::size_t i = 0;
				for (int y = box_.min_.y; y < box_.max_.y; ++y)
				for (int z = box_.min_.z; z < box_.max_.z; ++z)
				{
					const MaterialId* row = &region.at(box_.min_.x, y, z);
					for (int x = 0; x < size_.x; ++x)
						a[i++] = row[x] != kAir ? 1 : 0;
				}

				const std::size_t strideX = 1, strideZ = static_cast<std::size_t>(size_.x);
				const std::size_t strideY = strideZ * static_cast<std::size_t>(size_.z);

				slide(a, b, size_.x, strideX, radius);
				slide(b, a, size_.z, strideZ, radius);
				slide(a, b, size_.y, strideY, radius);
				sums_ = std::move(b);
			}

			/*!
				@brief
					Density at a voxel; 0 outside the box it was built for.
				@param x
					Voxel x
				@param y
					Voxel y
				@param z
					Voxel z
				@return
					Share of solid voxels around it, 0..1
				*/
			float at(int x, int y, int z) const
			{
				if (!box_.contains(x, y, z))
					return 0.f;
				return static_cast<float>(sums_[index(x, y, z)]) * scale_;
			}

			/*!
				@brief
					Central-difference density gradient; points from empty towards solid.
				@param x
					Voxel x
				@param y
					Voxel y
				@param z
					Voxel z
				@return
					The (unnormalised) gradient
				*/
			glm::vec3 gradient(int x, int y, int z) const
			{
				return {
					at(x + 1, y, z) - at(x - 1, y, z),
					at(x, y + 1, z) - at(x, y - 1, z),
					at(x, y, z + 1) - at(x, y, z - 1) };
			}

		private:

			/*!
				@brief
					Flat index of a voxel in sums_ (x fastest, then z, then y).
				@param x
					Voxel x
				@param y
					Voxel y
				@param z
					Voxel z
				@return
					Index into sums_
				*/
			std::size_t index(int x, int y, int z) const
			{
				return (static_cast<std::size_t>(y - box_.min_.y) * static_cast<std::size_t>(size_.z)
					+ static_cast<std::size_t>(z - box_.min_.z)) * static_cast<std::size_t>(size_.x)
					+ static_cast<std::size_t>(x - box_.min_.x);
			}

			/*!
				@brief
					Sliding-window sum along one axis: dst[i] = sum of src over [i - r, i + r], empty beyond the ends.
				@param src
					Input values
				@param dst
					Receives the window sums
				@param length
					Number of samples along the axis
				@param stride
					Distance between neighbours along the axis
				@param r
					Window half width
				*/
			void slide(const std::vector<std::uint16_t>& src, std::vector<std::uint16_t>& dst, int length, std::size_t stride, int r) const
			{
				const std::size_t total = src.size();
				const std::size_t lineSpan = stride * static_cast<std::size_t>(length);

				for (std::size_t block = 0; block < total; block += lineSpan)
				for (std::size_t offset = 0; offset < stride; ++offset)
				{
					const std::uint16_t* s = src.data() + block + offset;
					std::uint16_t* d = dst.data() + block + offset;

					std::uint32_t sum = 0;
					for (int k = 0; k <= r && k < length; ++k)
						sum += s[static_cast<std::size_t>(k) * stride];

					for (int k = 0; k < length; ++k)
					{
						d[static_cast<std::size_t>(k) * stride] = static_cast<std::uint16_t>(sum);
						if (k + r + 1 < length)
							sum += s[static_cast<std::size_t>(k + r + 1) * stride];
						if (k - r >= 0)
							sum -= s[static_cast<std::size_t>(k - r) * stride];
					}
				}
			}

			VoxelBox					box_;
			glm::ivec3					size_;
			float						scale_;
			std::vector<std::uint16_t>	sums_;
		};

		/*!
			@brief
				The centre point of a voxel in voxel units.
			@param x
				Voxel x
			@param y
				Voxel y
			@param z
				Voxel z
			@return
				(x, y, z) + 0.5
			*/
		glm::vec3 centreOf(int x, int y, int z)
		{
			return { static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, static_cast<float>(z) + 0.5f };
		}

		/** What one op function works with. */
		struct Context
		{
			VoxelRegion&	region_;
			const Stamp&	stamp_;
			const OpParams&	params_;
			const Density*	density_;		// null unless the op or its mask needs it
			VoxelBox		box_;			// the brush's bounds, inside the region
			std::size_t		changed_	{ 0 };

			/*!
				@brief
					Whether the op's mask (material, height, slope) lets it change this voxel.
				@param x
					Voxel x
				@param y
					Voxel y
				@param z
					Voxel z
				@param current
					The voxel's current material
				@return
					True when the voxel may change
				*/
			bool allowed(int x, int y, int z, MaterialId current) const
			{
				const Mask& mask = params_.mask_;
				if (mask.useMaterial_ && current != kAir && current != mask.material_)
					return false;
				if (mask.useHeight_ && (y < mask.minY_ || y > mask.maxY_))
					return false;

				if (mask.useSlope_ && density_)
				{
					// Measured on the density field, never on voxel heights: a
					// one-voxel step on flat ground would read as a cliff.
					const glm::vec3 g = density_->gradient(x, y, z);
					const float length = glm::length(g);
					if (length > 1e-6f)
					{
						const float slope = glm::degrees(std::acos(std::clamp(-g.y / length, -1.f, 1.f)));
						if (slope < mask.slopeMinDeg_ || slope > mask.slopeMaxDeg_)
							return false;
					}
				}
				return true;
			}

			/*!
				@brief
					Material for a newly filled voxel.

					With automatic on, new ground continues what it grows out of: what
					is below first (up to 6 voxels), then beside, then above; otherwise,
					or when nothing is found, the op's material.
				@param x
					Voxel x
				@param y
					Voxel y
				@param z
					Voxel z
				@param automatic
					Whether to take the material from neighbouring ground
				@return
					The material to write
				*/
			MaterialId fillMaterial(int x, int y, int z, bool automatic) const
			{
				if (!automatic)
					return params_.material_;

				for (int down = 1; down <= 6; ++down)
					if (const MaterialId m = region_.get(x, y - down, z); m != kAir)
						return m;

				constexpr std::array<glm::ivec3, 5> kBeside{ { { -1, 0, 0 }, { 1, 0, 0 }, { 0, 0, -1 }, { 0, 0, 1 }, { 0, 1, 0 } } };
				for (const glm::ivec3& d : kBeside)
					if (const MaterialId m = region_.get(x + d.x, y + d.y, z + d.z); m != kAir)
						return m;

				return params_.material_;
			}

			/*!
				@brief
					Writes a voxel and counts it as changed when the material differs.
				@param x
					Voxel x
				@param y
					Voxel y
				@param z
					Voxel z
				@param m
					Material to write
				*/
			void put(int x, int y, int z, MaterialId m)
			{
				MaterialId& voxel = region_.at(x, y, z);
				if (voxel == m)
					return;
				voxel = m;
				++changed_;
			}

			/*!
				@brief
					Calls fn(x, y, z, weight) for every voxel the brush covers, a row span at a time.
				@param gradual
					True to pass the falloff weight; false passes 1
				@param fn
					Callback per covered voxel
				*/
			template <typename Fn>
			void forCovered(bool gradual, const Fn& fn)
			{
				for (int y = box_.min_.y; y < box_.max_.y; ++y)
				for (int z = box_.min_.z; z < box_.max_.z; ++z)
				{
					// The covered run of the row, found once, not voxel by voxel.
					const auto [first, last] = stamp_.span(0, y, z);
					const int x0 = std::max(first, box_.min_.x), x1 = std::min(last, box_.max_.x);
					if (!gradual)
					{
						for (int x = x0; x < x1; ++x)
							fn(x, y, z, 1.f);
						continue;
					}

					// Stamp::falloff() written out, with what is constant along
					// the row taken out of it: this loop is the whole cost of a
					// gradual op on a large brush.
					const glm::vec3 e = glm::max(stamp_.extent(), glm::vec3{ 1e-3f });
					const float dy = static_cast<float>(y) + 0.5f - stamp_.centre_.y, dz = static_cast<float>(z) + 0.5f - stamp_.centre_.z;
					const float rowY = std::fabs(dy) / e.y, rowZ = std::fabs(dz) / e.z;
					for (int x = x0; x < x1; ++x)
					{
						const float dx = static_cast<float>(x) + 0.5f - stamp_.centre_.x;
						float reach;
						switch (stamp_.shape_)
						{
						case Shape::Sphere:		reach = std::sqrt(dx * dx + dy * dy + dz * dz) / e.x; break;
						case Shape::Cube:		reach = std::max({ std::fabs(dx) / e.x, rowY, rowZ }); break;
						default:				reach = std::max(std::sqrt(dx * dx + dz * dz) / e.x, rowY); break;
						}

						const float t = std::clamp((reach - 0.5f) * 2.f, 0.f, 1.f);
						fn(x, y, z, 1.f - t * t * (3.f - 2.f * t));
					}
				}
			}
		};

		// ------------------------------- Ops -------------------------------

		/*!
			@brief
				Fills every allowed empty voxel the brush covers.
			@param c
				The op context
			*/
		void opAdd(Context& c)
		{
			c.forCovered(false, [&](int x, int y, int z, float)
			{
				if (c.region_.at(x, y, z) == kAir && c.allowed(x, y, z, kAir))
					c.put(x, y, z, c.fillMaterial(x, y, z, c.params_.autoMaterial_));
			});
		}

		/*!
			@brief
				Empties every allowed solid voxel the brush covers.
			@param c
				The op context
			*/
		void opSubtract(Context& c)
		{
			c.forCovered(false, [&](int x, int y, int z, float)
			{
				const MaterialId m = c.region_.at(x, y, z);
				if (m != kAir && c.allowed(x, y, z, m))
					c.put(x, y, z, kAir);
			});
		}

		/*!
			@brief
				Fills empty voxels next to ground whose density, raised by strength x falloff, passes 0.5.
			@param c
				The op context
			*/
		void opGrow(Context& c)
		{
			c.forCovered(true, [&](int x, int y, int z, float weight)
			{
				if (c.region_.at(x, y, z) != kAir || !c.allowed(x, y, z, kAir))
					return;

				// Only next to existing ground: a brush waved through the air adds nothing.
				const float d = c.density_->at(x, y, z);
				if (d > 0.f && d + kSculptGain * c.params_.strength_ * weight > 0.5f)
					c.put(x, y, z, c.fillMaterial(x, y, z, c.params_.autoMaterial_));
			});
		}

		/*!
			@brief
				Empties solid voxels whose density, lowered by strength x falloff, drops below 0.5.
			@param c
				The op context
			*/
		void opShrink(Context& c)
		{
			c.forCovered(true, [&](int x, int y, int z, float weight)
			{
				const MaterialId m = c.region_.at(x, y, z);
				if (m == kAir || !c.allowed(x, y, z, m))
					return;
				if (c.density_->at(x, y, z) - kSculptGain * c.params_.strength_ * weight < 0.5f)
					c.put(x, y, z, kAir);
			});
		}

		/*!
			@brief
				Majority vote: a voxel follows its neighbourhood once the majority clears a margin strength lowers.
			@param c
				The op context
			*/
		void opSmooth(Context& c)
		{
			c.forCovered(true, [&](int x, int y, int z, float weight)
			{
				const MaterialId m = c.region_.at(x, y, z);
				if (!c.allowed(x, y, z, m))
					return;

				// A voxel follows the majority of its neighbourhood once the
				// majority is clear enough; strength lowers the bar.
				const float margin = kSmoothMargin * (1.f - std::clamp(c.params_.strength_ * weight, 0.f, 1.f));
				const float d = c.density_->at(x, y, z);

				if (m != kAir && d < 0.5f - margin)
					c.put(x, y, z, kAir);
				else if (m == kAir && d > 0.5f + margin)
					c.put(x, y, z, c.fillMaterial(x, y, z, true));
			});
		}

		/*!
			@brief
				Moves each covered column's top towards the plane, by a step scaled by strength and falloff.
			@param c
				The op context
			*/
		void opFlatten(Context& c)
		{
			const Stamp& stamp = c.stamp_;
			const OpParams& params = c.params_;

			// Voxel y is solid on a flat surface at height h iff y + 1 <= h.
			const int targetTop = static_cast<int>(std::floor(params.planeY_ + 1e-3f)) - 1;

			for (int z = c.box_.min_.z; z < c.box_.max_.z; ++z)
			for (int x = c.box_.min_.x; x < c.box_.max_.x; ++x)
			{
				// The brush is convex, so what it covers of a column is one run.
				const auto [first, last] = stamp.span(1, x, z);
				const int lo = std::max(first, c.box_.min_.y), hi = std::min(last, c.box_.max_.y) - 1;
				if (hi < lo)
					continue;

				const float weight = stamp.falloff({ static_cast<float>(x) + 0.5f, stamp.centre_.y, static_cast<float>(z) + 0.5f });
				if (weight <= 0.f)
					continue;
				const int step = std::max(1, static_cast<int>(std::lround(params.strength_ * weight * stamp.radius())));

				int top = lo - 1;
				for (int y = hi; y >= lo; --y)
					if (c.region_.at(x, y, z) != kAir) { top = y; break; }

				if (params.flattenMode_ != FlattenMode::Raise && top > targetTop)
				{
					const int newTop = std::max({ targetTop, top - step, lo - 1 });
					for (int y = top; y > newTop; --y)
					{
						const MaterialId m = c.region_.at(x, y, z);
						if (m != kAir && c.allowed(x, y, z, m))
							c.put(x, y, z, kAir);
					}
				}
				else if (params.flattenMode_ != FlattenMode::Lower && top < targetTop)
				{
					// Only on ground that is there: a brush held over a drop
					// must not leave a slab hanging in the air.
					if (top < lo && c.region_.get(x, lo - 1, z) == kAir)
						continue;

					const int newTop = std::min({ targetTop, top + step, hi });
					for (int y = top + 1; y <= newTop; ++y)
					{
						if (c.region_.at(x, y, z) == kAir && c.allowed(x, y, z, kAir))
							c.put(x, y, z, c.fillMaterial(x, y, z, params.autoMaterial_));
					}
				}
			}
		}

		/*!
			@brief
				Sets the op's material on every allowed solid voxel the brush covers.
			@param c
				The op context
			*/
		void opPaint(Context& c)
		{
			c.forCovered(false, [&](int x, int y, int z, float)
			{
				const MaterialId m = c.region_.at(x, y, z);
				if (m != kAir && c.allowed(x, y, z, m))
					c.put(x, y, z, c.params_.material_);
			});
		}

		/*!
			@brief
				Turns covered voxels of the source material into the op's material.
			@param c
				The op context
			*/
		void opReplace(Context& c)
		{
			c.forCovered(false, [&](int x, int y, int z, float)
			{
				const MaterialId m = c.region_.at(x, y, z);
				if (m == c.params_.source_ && c.allowed(x, y, z, m))
					c.put(x, y, z, c.params_.material_);
			});
		}

		/*!
			@brief
				Sets every allowed covered voxel, solid or empty, to the op's material.
			@param c
				The op context
			*/
		void opFill(Context& c)
		{
			c.forCovered(false, [&](int x, int y, int z, float)
			{
				if (c.allowed(x, y, z, c.region_.at(x, y, z)))
					c.put(x, y, z, c.params_.material_);
			});
		}

		/*!
			@brief
				Smooth value noise in 0..1 over columns, bilinear over 3-voxel cells.

				Rounding a column's new height against it, rather than against a
				constant, lets changes smaller than a voxel add up over a stroke, in
				patches rather than as speckle.
			@param x
				Column x
			@param z
				Column z
			@param seed
				Noise seed
			@return
				Dither threshold in [0, 0.999]
			*/
		float ditherAt(int x, int z, std::uint64_t seed)
		{
			const auto corner = [seed](int cx, int cz)
			{
				std::uint64_t h = seed ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cx)) * 0x9E3779B97F4A7C15ull)
					^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cz)) * 0xC2B2AE3D27D4EB4Full);
				h ^= h >> 31; h *= 0xD6E8FEB86659FD93ull; h ^= h >> 32;
				return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0x1000000u);
			};

			constexpr int kCell = 3;
			const int cx = static_cast<int>(std::floor(static_cast<float>(x) / kCell));
			const int cz = static_cast<int>(std::floor(static_cast<float>(z) / kCell));
			const float u = static_cast<float>(x - cx * kCell) / kCell, v = static_cast<float>(z - cz * kCell) / kCell;

			const float a = corner(cx, cz) * (1.f - u) + corner(cx + 1, cz) * u;
			const float b = corner(cx, cz + 1) * (1.f - u) + corner(cx + 1, cz + 1) * u;
			return std::clamp(a * (1.f - v) + b * v, 0.f, 0.999f);
		}

		/*!
			@brief
				Runs hydraulic and thermal erosion on a padded height map of the patch and writes the change back.

				Changes are weighted by falloff, rounded against ditherAt, and limited
				to voxels the brush covers.
			@param c
				The op context
			*/
		void opErode(Context& c)
		{
			VOX_PROFILE_SCOPE_C("Brush erode", profile::colour::kVoxel);

			const Stamp& stamp = c.stamp_;
			const VoxelBox& whole = c.region_.box();

			// The patch of ground the passes run over: the brush's footprint and a rim.
			VoxelBox patch = stamp.bounds().expanded(kErodePad).clipped(whole);
			patch.min_.y = whole.min_.y;
			patch.max_.y = whole.max_.y;
			const int width = patch.max_.x - patch.min_.x, depth = patch.max_.z - patch.min_.z;
			if (width < 4 || depth < 4)
				return;

			// The top surface as a height map, in voxels. A column with no
			// surface inside the region - open air, or rock right to the top -
			// takes part so its neighbours see a sensible height, and is not
			// written back.
			HeightField heights(width, depth, 1.f);
			std::vector<std::uint8_t> open(static_cast<std::size_t>(width) * static_cast<std::size_t>(depth), 0);

			for (int z = 0; z < depth; ++z)
			for (int x = 0; x < width; ++x)
			{
				const int wx = patch.min_.x + x, wz = patch.min_.z + z;
				int top = patch.min_.y - 1;
				for (int y = patch.max_.y - 1; y >= patch.min_.y; --y)
					if (c.region_.at(wx, y, wz) != kAir) { top = y; break; }

				heights.at(x, z) = static_cast<float>(top + 1);
				open[heights.index(x, z)] = top >= patch.min_.y && top < patch.max_.y - 1 ? 1 : 0;
			}

			const HeightField before = heights;
			HeightField sediment(width, depth, 1.f);

			Recipe::Erosion erosion;
			// Under one droplet per column: the stamps of a stroke overlap four
			// deep, so the ground still sees a few, and the pass stays affordable
			// on the largest brush.
			erosion.droplets_ = static_cast<int>(0.8f * std::clamp(c.params_.strength_, 0.f, 1.f) * static_cast<float>(width) * static_cast<float>(depth));
			erosion.radius_ = 2;
			erosion.lifetime_ = 32;
			erosion.minRelief_ = kErodeMinRelief;
			erosion.thermalIterations_ = std::max(1, static_cast<int>(8.f * c.params_.strength_));
			erosion.rockTalusDeg_ = 46.f;
			erosion.sedimentTalusDeg_ = 33.f;

			erodeHydraulic(heights, sediment, erosion, c.params_.seed_);
			erodeThermal(heights, sediment, erosion);

			for (int z = 0; z < depth; ++z)
			for (int x = 0; x < width; ++x)
			{
				if (!open[heights.index(x, z)])
					continue;

				const int wx = patch.min_.x + x, wz = patch.min_.z + z;
				const float weight = stamp.falloff({ static_cast<float>(wx) + 0.5f, stamp.centre_.y, static_cast<float>(wz) + 0.5f });
				if (weight <= 0.f)
					continue;

				const float was = before.at(x, z);
				const float now = was + (heights.at(x, z) - was) * weight;
				const int oldTop = static_cast<int>(was) - 1;
				const int newTop = std::clamp(static_cast<int>(std::floor(now + ditherAt(wx, wz, c.params_.seed_))) - 1,
					patch.min_.y - 1, patch.max_.y - 1);

				// Only what the brush covers changes, so a flat brush held at
				// the surface cannot dig a shaft below itself.
				for (int y = oldTop; y > newTop; --y)
				{
					const MaterialId m = c.region_.at(wx, y, wz);
					if (m != kAir && stamp.covers(centreOf(wx, y, wz)) && c.allowed(wx, y, wz, m))
						c.put(wx, y, wz, kAir);
				}
				for (int y = oldTop + 1; y <= newTop; ++y)
				{
					if (c.region_.at(wx, y, wz) == kAir && stamp.covers(centreOf(wx, y, wz)))
						c.put(wx, y, wz, c.fillMaterial(wx, y, wz, true));
				}
			}
		}

		// ------------------------------ Table ------------------------------

		struct Entry
		{
			OpInfo	info_;
			bool	density_;				// needs the Density field
			int		(*padding_)(const Stamp&);	// context beyond the brush's bounds, in voxels
			void	(*run_)(Context&);
		};

		/*!
			@brief
				Context padding for ops that read no neighbourhood.
			@param Stamp
				The brush application (unused)
			@return
				1 voxel
			*/
		int noPadding(const Stamp&) { return 1; }

		/*!
			@brief
				Context padding for ops that read the density field.
			@param stamp
				The brush application
			@return
				filterRadius(stamp) + 1 voxels
			*/
		int filterPadding(const Stamp& stamp) { return filterRadius(stamp) + 1; }

		/*!
			@brief
				Context padding for Erode.
			@param Stamp
				The brush application (unused)
			@return
				kErodePad + 1 voxels
			*/
		int erodePadding(const Stamp&) { return kErodePad + 1; }

		// In enum order; ops() and info() index it by Op.
		constexpr std::array<Entry, static_cast<std::size_t>(Op::Count)> kOps{ {
			{ { Op::Add,		"add",		false,	true  }, false,	noPadding,		opAdd },
			{ { Op::Subtract,	"subtract",	false,	true  }, false,	noPadding,		opSubtract },
			{ { Op::Grow,		"grow",		true,	true  }, true,	filterPadding,	opGrow },
			{ { Op::Shrink,		"shrink",	true,	true  }, true,	filterPadding,	opShrink },
			{ { Op::Smooth,		"smooth",	true,	true  }, true,	filterPadding,	opSmooth },
			{ { Op::Flatten,	"flatten",	true,	true  }, false,	noPadding,		opFlatten },
			{ { Op::Paint,		"paint",	false,	false }, false,	noPadding,		opPaint },
			{ { Op::Replace,	"replace",	false,	false }, false,	noPadding,		opReplace },
			{ { Op::Erode,		"erode",	true,	true  }, false,	erodePadding,	opErode },
			{ { Op::Fill,		"fill",		false,	true  }, false,	noPadding,		opFill },
		} };

		/*!
			@brief
				The table row of an op; an out-of-range op falls back to the first row.
			@param op
				Op to look up
			@return
				Its table entry
			*/
		const Entry& entry(Op op)
		{
			return kOps[static_cast<std::size_t>(op) < kOps.size() ? static_cast<std::size_t>(op) : 0];
		}

		/*!
			@brief
				Whether a stamp needs the density field: the op reads it, or the slope mask is on.
			@param e
				The op's table entry
			@param params
				The op settings
			@return
				True when a Density must be built
			*/
		bool wantsDensity(const Entry& e, const OpParams& params)
		{
			return e.density_ || params.mask_.useSlope_;
		}
	}

	/*!
		@brief
			Every op's info, in enum order, copied once from the table.
		@return
			The op infos
		*/
	std::span<const OpInfo> ops()
	{
		static const std::array<OpInfo, kOps.size()> infos = []
		{
			std::array<OpInfo, kOps.size()> out{};
			for (std::size_t i = 0; i < kOps.size(); ++i)
				out[i] = kOps[i].info_;
			return out;
		}();
		return infos;
	}

	/*!
		@brief
			The table row of one op.
		@param op
			Op to look up
		@return
			Its OpInfo
		*/
	const OpInfo& info(Op op)
	{
		return entry(op).info_;
	}

	/*!
		@brief
			The brush's bounds expanded by the op's padding (and the filter's, when density is needed).
		@param stamp
			The brush application
		@param params
			The op and its settings
		@return
			The box to read
		*/
	VoxelBox contextBox(const Stamp& stamp, const OpParams& params)
	{
		const Entry& e = entry(params.op_);
		const int pad = std::max(e.padding_(stamp), wantsDensity(e, params) ? filterPadding(stamp) : 0);
		return stamp.bounds().expanded(pad);
	}

	/*!
		@brief
			Builds the density field if needed and runs the op over the brush's bounds within the region.
		@param region
			Voxels to edit, in place
		@param stamp
			The brush application
		@param params
			The op and its settings
		@return
			Voxels changed
		*/
	std::size_t apply(VoxelRegion& region, const Stamp& stamp, const OpParams& params)
	{
		VOX_PROFILE_FUNCTION_C(profile::colour::kVoxel);

		const Entry& e = entry(params.op_);
		const VoxelBox box = stamp.bounds().clipped(region.box());
		if (box.empty())
			return 0;

		std::optional<Density> density;
		if (wantsDensity(e, params))
			density.emplace(region, contextBox(stamp, params), filterRadius(stamp));

		Context context{ region, stamp, params, density ? &*density : nullptr, box };
		e.run_(context);
		return context.changed_;
	}
}
