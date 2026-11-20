/*************************************************************************************************************
\file       voxel_edit_self_check.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Startup self-check for voxel editing: region read/write, the grid raycast, every brush op,
            the undo history, block transforms and the batched LoD rebuild.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "voxel_edit_self_check.hpp"

// Voxel
#include "../../voxel/types/material_table.hpp"
#include "../../voxel/world/world.hpp"
#include "../../voxel/edit/brush.hpp"
#include "../../voxel/edit/brush_ops.hpp"
#include "../../voxel/edit/edit_history.hpp"
#include "../../voxel/edit/region_transform.hpp"
#include "../../voxel/edit/voxel_raycast.hpp"
#include "../../voxel/edit/voxel_region.hpp"

// stds
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <vector>


namespace engine::checks
{
	namespace
	{
		using namespace voxel;
		using namespace voxel::edit;

		// Deliberately not a multiple of 32, so the rounding-up voxels exist.
		constexpr glm::ivec3 kWorldVoxels{ 100, 70, 100 };
		constexpr int kGround = 32;		// rows 0..31 are solid: exactly the bottom layer of sections

		/*!
			@brief
				Gives the box covering every level-0 section of a world, rounding-up
				voxels included.
			@param world
				World to cover
			@return
				Voxel box from the origin to the end of the last section
			*/
		VoxelBox wholeWorld(const World& world)
		{
			return { glm::ivec3{ 0 }, world.sectionsAt(0) * kSectionSize };
		}

		/*!
			@brief
				Builds a world with flat ground, written through the region path.

				The region overshoots the world on purpose.
			@param ground
				Material of the ground rows
			@return
				The new world
			*/
		World makeGround(MaterialId ground)
		{
			World world(WorldDims::fromVoxels(kWorldVoxels, 0.1f, glm::vec3{ 0.f }));
			VoxelRegion region(VoxelBox{ { -4, -4, -4 }, { 128, kGround, 128 } }, ground);
			write(world, region);
			return world;
		}

		/*!
			@brief
				Compares every level-0 voxel of two worlds.
			@param a
				First world
			@param b
				Second world
			@return
				True when both hold the same voxels
			*/
		bool sameVoxels(const World& a, const World& b)
		{
			VoxelRegion ra, rb;
			read(a, wholeWorld(a), ra);
			read(b, wholeWorld(b), rb);
			return ra.volume() == rb.volume() && std::memcmp(ra.data(), rb.data(), ra.volume()) == 0;
		}

		/*!
			@brief
				Counts the non-air voxels of a region.
			@param region
				Region to count
			@return
				Number of solid voxels
			*/
		std::size_t solidCount(const VoxelRegion& region)
		{
			return static_cast<std::size_t>(std::count_if(region.data(), region.data() + region.volume(),
				[](MaterialId m) { return m != kAir; }));
		}

		/*!
			@brief
				Finds the highest solid voxel of a column in a region.
			@param region
				Region to search
			@param x
				Column x in voxels
			@param z
				Column z in voxels
			@return
				Y of the top solid voxel, or one below the region's floor when the
				column is empty
			*/
		int columnTop(const VoxelRegion& region, int x, int z)
		{
			for (int y = region.box().max_.y - 1; y >= region.box().min_.y; --y)
				if (region.at(x, y, z) != kAir)
					return y;
			return region.box().min_.y - 1;
		}

		/*!
			@brief
				Makes a spherical brush stamp.
			@param centre
				Centre in voxel units
			@param radius
				Radius in voxels
			@return
				The stamp
			*/
		Stamp sphereAt(const glm::vec3& centre, float radius)
		{
			Stamp s;
			s.centre_ = centre;
			s.half_ = glm::vec3{ radius };
			s.shape_ = Shape::Sphere;
			return s;
		}
	}

	/*!
		@brief
			Exercises voxel editing's pure pieces and prints "[voxel-edit-check]"
			results. Never throws.
		*/
	void runVoxelEditSelfCheck()
	{
		int failures = 0;

		const auto check = [&failures](bool ok, const char* what)
		{
			if (!ok)
			{
				std::cerr << "[voxel-edit-check] FAILED: " << what << "\n";
				++failures;
			}
		};

		try
		{
			const MaterialTable materials = MaterialTable::builtin();
			const MaterialId stone = materials.id("stone");
			const MaterialId grass = materials.id("grass");
			const MaterialId water = materials.id("water");

			// ------------------------- Region IO --------------------------
			//
			// A brush is read, change, write. If the write disagrees with
			// voxel-at-a-time set() by one voxel, every tool is wrong; if it
			// touches sections that did not change, a stroke remeshes and
			// snapshots the whole neighbourhood.
			{
				World byRegion = makeGround(stone);

				World byVoxel(WorldDims::fromVoxels(kWorldVoxels, 0.1f, glm::vec3{ 0.f }));
				for (int y = 0; y < kGround; ++y)
				for (int z = 0; z < kWorldVoxels.z; ++z)
				for (int x = 0; x < kWorldVoxels.x; ++x)
					byVoxel.set(x, y, z, stone);
				for (std::size_t i = 0; i < byVoxel.sectionCount(0); ++i)
					byVoxel.editSectionAt(0, i).tryCollapse();

				check(sameVoxels(byRegion, byVoxel), "a region write matches voxel-by-voxel set()");
				check(byRegion.section(0, 1, 0, 1).isUniform(), "a section a write fills completely collapses to uniform");
				check(byRegion.section(0, 1, 2, 1).isAir(), "a section a write does not change stays untouched air");
				check(byRegion.get(kWorldVoxels.x, 5, 5) == kAir && byRegion.section(0, 3, 0, 0).get(kWorldVoxels.x, 5, 5) == kAir,
					"a write never fills the voxels past the world's logical size");

				VoxelRegion again;
				read(byRegion, VoxelBox{ { 10, 20, 10 }, { 60, 40, 60 } }, again);
				check(write(byRegion, again).changed_.empty(), "writing back what was read changes no section");

				// One voxel on a section's -x plane: that section changed, and
				// the one across the plane must remesh its border faces.
				again.at(32, 25, 40) = grass;
				WriteResult r = write(byRegion, again);
				check(r.changed_.size() == 1 && r.changed_[0] == glm::ivec3{ 1, 0, 1 } && r.voxelsChanged_ == 1,
					"a one-voxel change reports exactly its section");
				check(r.neighbours_.size() == 1 && r.neighbours_[0] == glm::ivec3{ 0, 0, 1 },
					"a change on a section's border plane reports the neighbour across it");

				again.at(40, 25, 40) = grass;
				r = write(byRegion, again);
				check(r.changed_.size() == 1 && r.neighbours_.empty(), "a change inside a section reports no neighbour");

				int touched = 0;
				again.at(41, 25, 40) = grass;
				write(byRegion, again, [&touched, stone](const glm::ivec3&, const Section& before)
				{
					touched += before.get(41, 25, 40) == stone ? 1 : 100;
				});
				check(touched == 1, "BeforeChange sees each changed section once, as it was before the write");

				VoxelRegion outside;
				read(byRegion, VoxelBox{ { -8, -8, -8 }, { 4, 4, 4 } }, outside);
				check(outside.get(-1, 0, 0) == kAir && outside.get(0, 0, 0) == stone, "a read that reaches outside the world is air there");
			}

			// -------------------------- Raycast ---------------------------
			//
			// The brush cursor. Checked against the definition rather than
			// another traversal: the voxel is solid, the ray really enters it
			// through the reported face, and nothing solid lies before it.
			{
				World world = makeGround(stone);

				// Bumps and a pit, so rays meet faces on every axis.
				VoxelRegion region;
				read(world, VoxelBox{ { 20, 10, 20 }, { 80, 60, 80 } }, region);
				OpParams params;
				params.material_ = stone;
				params.op_ = Op::Add;
				apply(region, sphereAt({ 40.5f, 34.5f, 40.5f }, 9.f), params);
				params.op_ = Op::Subtract;
				apply(region, sphereAt({ 60.5f, 28.5f, 55.5f }, 7.f), params);
				write(world, region);

				const auto solidAt = [&](const glm::vec3& worldPoint)
				{
					const glm::ivec3 v = glm::ivec3(glm::floor(worldPoint / 0.1f));
					return world.get(v.x, v.y, v.z) != kAir;
				};

				std::uint32_t state = 12345u;
				const auto random = [&state]
				{
					state = state * 1664525u + 1013904223u;
					return static_cast<float>(state >> 8) / static_cast<float>(1u << 24);
				};

				int hits = 0, wrong = 0;
				for (int i = 0; i < 200; ++i)
				{
					util::geom::Ray ray;
					ray.origin_ = { random() * 14.f - 2.f, 5.f + random() * 6.f, random() * 14.f - 2.f };
					const glm::vec3 target{ 1.f + random() * 8.f, random() * 4.f, 1.f + random() * 8.f };
					ray.direction_ = glm::normalize(target - ray.origin_);

					const std::optional<VoxelHit> hit = raycast(world, ray);
					const float until = hit ? hit->distance_ - 0.005f : 25.f;
					bool clear = true;
					for (float t = 0.f; t < until && clear; t += 0.004f)
						clear = !solidAt(ray.at(t));
					if (!clear)
						++wrong;

					if (!hit)
						continue;
					++hits;

					if (world.get(hit->voxel_.x, hit->voxel_.y, hit->voxel_.z) == kAir)
						++wrong;

					// The hit point lies on the face the normal names.
					for (int a = 0; a < 3; ++a)
					{
						if (hit->normal_[a] == 0)
							continue;
						const float face = static_cast<float>(hit->voxel_[a] + (hit->normal_[a] > 0 ? 1 : 0)) * 0.1f;
						if (std::fabs(hit->point_[a] - face) > 0.002f)
							++wrong;
					}
				}
				check(hits > 100, "rays aimed at the ground hit it");
				check(wrong == 0, "every hit is the first solid voxel, entered through the reported face");

				util::geom::Ray down;
				down.origin_ = { 1.55f, 6.f, 1.55f };
				down.direction_ = { 0.f, -1.f, 0.f };
				const std::optional<VoxelHit> floor = raycast(world, down);
				check(floor && floor->voxel_ == glm::ivec3{ 15, kGround - 1, 15 } && floor->normal_ == glm::ivec3{ 0, 1, 0 },
					"a ray straight down hits the top voxel of its column through the +y face");
				check(floor && std::fabs(floor->distance_ - (6.f - 0.1f * kGround)) < 1e-3f, "and reports the distance in metres");

				// A sheet of water over the ground.
				VoxelRegion lake(VoxelBox{ { 0, kGround, 0 }, { kWorldVoxels.x, kGround + 3, kWorldVoxels.z } }, water);
				write(world, lake);
				const std::optional<VoxelHit> onWater = raycast(world, down);
				RaycastOptions through;
				through.passThrough_ = water;
				const std::optional<VoxelHit> onBed = raycast(world, down, through);
				check(onWater && onWater->material_ == water && onWater->voxel_.y == kGround + 2, "a ray stops on water");
				check(onBed && onBed->material_ == stone && onBed->voxel_.y == kGround - 1, "unless told to pass through it");

				util::geom::Ray away = down;
				away.direction_ = { 0.f, 1.f, 0.f };
				check(!raycast(world, away), "a ray that leaves the world hits nothing");

				// A stroke aims at the terrain it began on. Without this a brush
				// that adds ground hits its own last stamp, which is nearer the
				// eye, and the stroke climbs towards the camera.
				World drawn = makeGround(stone);
				SectionOverrides before;
				OpParams add;
				add.op_ = Op::Add;
				add.material_ = stone;
				const Stamp blob = sphereAt({ 15.5f, static_cast<float>(kGround), 15.5f }, 10.f);
				VoxelRegion stamped;
				read(drawn, contextBox(blob, add), stamped);
				apply(stamped, blob, add);
				write(drawn, stamped, [&](const glm::ivec3& s, const Section& was) { before.emplace(drawn.sectionIndex(0, s.x, s.y, s.z), was.clone()); });

				const std::optional<VoxelHit> onBlob = raycast(drawn, down);
				RaycastOptions asBegun;
				asBegun.overrides_ = &before;
				const std::optional<VoxelHit> onGround = raycast(drawn, down, asBegun);
				check(onBlob && onBlob->voxel_.y > kGround + 5, "a ray at the live terrain hits what a stroke has added");
				check(onGround && onGround->voxel_ == glm::ivec3{ 15, kGround - 1, 15 }, "a ray at the terrain as the stroke found it goes through what the stroke added");
			}

			// ---------------------------- Ops -----------------------------
			{
				const VoxelBox box{ { 0, 0, 0 }, { 64, 64, 64 } };
				const glm::vec3 centre{ 32.f, 32.f, 32.f };
				OpParams params;
				params.material_ = stone;

				// Add and Subtract are the shape itself.
				VoxelRegion air(box);
				params.op_ = Op::Add;
				const std::size_t added = apply(air, sphereAt(centre, 12.f), params);
				const double ideal = 4.0 / 3.0 * 3.14159265358979 * 12.0 * 12.0 * 12.0;
				check(std::fabs(static_cast<double>(added) - ideal) < 0.03 * ideal, "Add fills a sphere's volume");
				check(solidCount(air) == added, "Add reports what it changed");
				check(air.at(32, 32, 44) == kAir && air.at(32, 32, 43) == stone, "Add covers exactly the voxels whose centres the brush contains");

				params.op_ = Op::Subtract;
				apply(air, sphereAt(centre, 12.f), params);
				check(solidCount(air) == 0, "Subtract removes what Add put there");

				Stamp cube;
				cube.centre_ = centre;
				cube.half_ = { 4.f, 2.f, 8.f };
				cube.shape_ = Shape::Cube;
				params.op_ = Op::Fill;
				check(apply(air, cube, params) == 8u * 4u * 16u, "a cube brush is a box with three independent half extents");

				Stamp cylinder = cube;
				cylinder.shape_ = Shape::Cylinder;
				VoxelRegion tube(box);
				apply(tube, cylinder, params);
				check(tube.at(32, 32, 36) == kAir && tube.at(35, 32, 32) == stone && tube.at(32, 34, 32) == kAir,
					"a cylinder brush stands upright: radius from x, half height from y");

				// Flat ground for the rest.
				VoxelRegion ground(box);
				for (int y = 0; y < 32; ++y)
				for (int z = 0; z < 64; ++z)
				for (int x = 0; x < 64; ++x)
					ground.at(x, y, z) = stone;
				const std::size_t groundSolid = solidCount(ground);

				// Paint and Replace never move the surface.
				VoxelRegion painted = ground;
				params.op_ = Op::Paint;
				params.material_ = grass;
				const std::size_t repainted = apply(painted, sphereAt(centre, 8.f), params);
				check(repainted > 0 && solidCount(painted) == groundSolid, "Paint changes materials and leaves every voxel solid or empty as it was");
				check(painted.at(32, 33, 32) == kAir, "Paint does not fill air");

				params.op_ = Op::Replace;
				params.source_ = grass;
				params.material_ = water;
				apply(painted, sphereAt(centre, 4.f), params);
				check(painted.at(32, 30, 32) == water && painted.at(32, 25, 32) == grass && painted.at(32, 20, 32) == stone,
					"Replace changes its source material only, and only inside the brush");

				// A mask narrows Paint to a band of rows.
				VoxelRegion banded = ground;
				params = OpParams{};
				params.op_ = Op::Paint;
				params.material_ = grass;
				params.mask_.useHeight_ = true;
				params.mask_.minY_ = 30;
				params.mask_.maxY_ = 31;
				apply(banded, sphereAt(centre, 8.f), params);
				check(banded.at(32, 31, 32) == grass && banded.at(32, 29, 32) == stone, "a height mask limits a brush to its rows");

				// Smooth: flat ground is already smooth; a spike and a pinhole are not.
				params = OpParams{};
				params.op_ = Op::Smooth;
				params.material_ = stone;
				VoxelRegion smooth = ground;
				check(apply(smooth, sphereAt(centre, 10.f), params) == 0, "Smooth leaves flat ground alone");

				smooth.at(32, 32, 32) = stone;
				smooth.at(32, 33, 32) = stone;
				smooth.at(28, 31, 28) = kAir;
				apply(smooth, sphereAt(centre, 10.f), params);
				check(smooth.at(32, 33, 32) == kAir && smooth.at(32, 32, 32) == kAir, "Smooth removes a spike");
				check(smooth.at(28, 31, 28) != kAir, "Smooth fills a pinhole");

				// Grow and Shrink move the surface, and only next to ground.
				params.op_ = Op::Grow;
				VoxelRegion grown = ground;
				apply(grown, sphereAt(centre, 10.f), params);
				check(columnTop(grown, 32, 32) > 31, "Grow raises the ground under the brush");
				check(columnTop(grown, 5, 5) == 31, "and nowhere else");

				VoxelRegion empty(box);
				check(apply(empty, sphereAt(centre, 10.f), params) == 0, "Grow adds nothing in empty air");

				params.op_ = Op::Shrink;
				VoxelRegion shrunk = ground;
				apply(shrunk, sphereAt(centre, 10.f), params);
				check(columnTop(shrunk, 32, 32) < 31, "Shrink lowers the ground under the brush");

				params.strength_ = 0.f;
				VoxelRegion untouched = ground;
				check(apply(untouched, sphereAt(centre, 10.f), params) == 0, "at zero strength Shrink does nothing to flat ground");

				// Flatten brings a bumpy surface onto its plane.
				VoxelRegion bumpy = ground;
				for (int z = 0; z < 64; ++z)
				for (int x = 0; x < 64; ++x)
				{
					const int top = 31 + static_cast<int>(std::lround(4.0 * std::sin(x * 0.4) * std::cos(z * 0.3)));
					for (int y = 26; y < 40; ++y)
						bumpy.at(x, y, z) = y <= top ? stone : kAir;
				}

				params = OpParams{};
				params.op_ = Op::Flatten;
				params.material_ = stone;
				params.planeY_ = 32.f;
				Stamp disc;
				disc.centre_ = { 32.f, 32.f, 32.f };
				disc.half_ = { 14.f, 8.f, 14.f };
				disc.shape_ = Shape::Cylinder;

				VoxelRegion flat = bumpy;
				apply(flat, disc, params);
				bool level = true;
				for (int z = 28; z < 36; ++z)
				for (int x = 28; x < 36; ++x)
					level = level && columnTop(flat, x, z) == 31;
				check(level, "Flatten brings the ground under the brush onto the plane");

				params.flattenMode_ = FlattenMode::Lower;
				VoxelRegion lowered = bumpy;
				apply(lowered, disc, params);
				bool neverRaised = true, cutDown = true;
				for (int z = 20; z < 44; ++z)
				for (int x = 20; x < 44; ++x)
				{
					neverRaised = neverRaised && columnTop(lowered, x, z) <= columnTop(bumpy, x, z);
					if ((x - 32) * (x - 32) + (z - 32) * (z - 32) < 36)
						cutDown = cutDown && columnTop(lowered, x, z) <= 31;
				}
				check(neverRaised && cutDown, "Flatten in Lower mode cuts down to the plane and never fills");

				VoxelRegion hanging(box);
				params.flattenMode_ = FlattenMode::Both;
				check(apply(hanging, disc, params) == 0, "Flatten builds nothing over a drop");

				// Erode: the same stroke gives the same terrain, stays inside
				// the brush, moves something on a slope, and leaves no spikes.
				VoxelRegion slope(VoxelBox{ { 0, 0, 0 }, { 96, 64, 96 } });
				for (int z = 0; z < 96; ++z)
				for (int x = 0; x < 96; ++x)
				{
					const int top = 12 + x / 3 + static_cast<int>(std::lround(2.0 * std::sin(z * 0.35)));
					for (int y = 0; y <= top; ++y)
						slope.at(x, y, z) = stone;
				}

				params = OpParams{};
				params.op_ = Op::Erode;
				params.material_ = stone;
				params.seed_ = 7;
				const Stamp erodeBrush = sphereAt({ 48.f, 28.f, 48.f }, 24.f);

				VoxelRegion erodedA = slope, erodedB = slope;
				const std::size_t moved = apply(erodedA, erodeBrush, params);
				apply(erodedB, erodeBrush, params);
				check(moved > 0, "Erode changes a slope");
				check(std::memcmp(erodedA.data(), erodedB.data(), erodedA.volume()) == 0, "Erode is a function of its inputs");

				bool inside = true, spikes = false;
				for (int z = 1; z < 95; ++z)
				for (int x = 1; x < 95; ++x)
				{
					const int top = columnTop(erodedA, x, z);
					const float dx = static_cast<float>(x) + 0.5f - 48.f, dz = static_cast<float>(z) + 0.5f - 48.f;
					if (dx * dx + dz * dz > 24.f * 24.f && top != columnTop(slope, x, z))
						inside = false;

					const int lowest = std::min({ columnTop(erodedA, x - 1, z), columnTop(erodedA, x + 1, z), columnTop(erodedA, x, z - 1), columnTop(erodedA, x, z + 1) });
					const int highest = std::max({ columnTop(erodedA, x - 1, z), columnTop(erodedA, x + 1, z), columnTop(erodedA, x, z - 1), columnTop(erodedA, x, z + 1) });
					if (top > highest + 3 || top < lowest - 3)
						spikes = true;
				}
				check(inside, "Erode changes nothing outside the brush");
				check(!spikes, "Erode leaves no spike or shaft");

				check(ops().size() == static_cast<std::size_t>(Op::Count), "the op table has one row per Op");
				for (std::size_t i = 0; i < ops().size(); ++i)
					check(ops()[i].id_ == static_cast<Op>(i), "the op table is in enum order");
			}

			// -------------------------- History ---------------------------
			//
			// Undo is only worth having if it is exact.
			{
				World world = makeGround(stone);
				World original = makeGround(stone);

				History history;
				const BeforeChange touch = [&history](const glm::ivec3& s, const Section& before) { history.touch(s, before); };

				const auto stroke = [&](History::Id id, const glm::vec3& centre, Op op)
				{
					history.begin(id, "stroke");
					OpParams params;
					params.op_ = op;
					params.material_ = grass;
					for (int i = 0; i < 3; ++i)
					{
						const Stamp stamp = sphereAt(centre + glm::vec3{ 6.f * static_cast<float>(i), 0.f, 0.f }, 9.f);
						VoxelRegion region;
						read(world, contextBox(stamp, params), region);
						apply(region, stamp, params);
						write(world, region, touch);
					}
					return history.commit(world);
				};

				check(stroke(1, { 40.f, 30.f, 40.f }, Op::Subtract), "a stroke that changes voxels is recorded");
				World afterFirst(world.dims());
				{
					VoxelRegion all;
					read(world, wholeWorld(world), all);
					write(afterFirst, all);
				}
				check(stroke(2, { 50.f, 33.f, 60.f }, Op::Add), "and so is the next");
				check(!sameVoxels(world, afterFirst), "the second stroke changed the world");

				check(!history.undo(world).empty() && sameVoxels(world, afterFirst), "undo restores the world before the last stroke, exactly");
				check(!history.undo(world).empty() && sameVoxels(world, original), "and the one before that");
				check(history.undo(world).empty(), "undo with nothing left does nothing");
				check(world.section(0, 0, 2, 0).isAir() && world.section(0, 1, 0, 1).isUniform(), "restored sections are uniform again where they were");

				history.redo(world);
				check(sameVoxels(world, afterFirst), "redo reapplies a stroke, exactly");
				check(history.contains(1) && history.contains(2), "undone strokes wait on the redo stack");

				check(stroke(3, { 30.f, 30.f, 30.f }, Op::Paint), "a new stroke after an undo is recorded");
				check(!history.contains(2) && history.redoId() == 0, "and drops what could have been redone");

				history.begin(4, "empty");
				check(!history.commit(world) && !history.contains(4), "a stroke that changes nothing leaves no entry");

				// Rolling back the open transaction is how a live preview re-applies itself.
				World beforePreview(world.dims());
				{
					VoxelRegion all;
					read(world, wholeWorld(world), all);
					write(beforePreview, all);
				}
				history.begin(5, "preview");
				VoxelRegion block(VoxelBox{ { 10, 40, 10 }, { 50, 60, 50 } }, grass);
				write(world, block, touch);
				check(!history.rollbackOpen(world).empty() && sameVoxels(world, beforePreview), "rolling back the open transaction restores the world");
				check(history.isOpen(), "and keeps it open");
				history.commit(world);

				// Over budget, the oldest go first and the newest always stays.
				History small(1);
				const BeforeChange touchSmall = [&small](const glm::ivec3& s, const Section& before) { small.touch(s, before); };
				for (History::Id id = 1; id <= 3; ++id)
				{
					small.begin(id, "big");
					VoxelRegion slab(VoxelBox{ { 0, 40 + static_cast<int>(id), 0 }, { 64, 41 + static_cast<int>(id), 64 } }, grass);
					write(world, slab, touchSmall);
					small.commit(world);
				}
				check(small.undoCount() == 1 && small.contains(3) && !small.contains(1), "over budget the history keeps only the newest stroke");
			}

			// ------------------------- Transform --------------------------
			//
			// Moving terrain is copying it through a rotation. Nearest
			// neighbour keeps materials; a quarter turn has to be exact, or
			// four of them would erode the block.
			{
				// An L of two materials: no symmetry for a wrong turn to hide behind.
				VoxelRegion block(VoxelBox{ { 10, 10, 10 }, { 22, 16, 18 } });
				for (int y = 10; y < 16; ++y)
				for (int z = 10; z < 18; ++z)
				for (int x = 10; x < 22; ++x)
					if (x < 14 || z < 13)
						block.at(x, y, z) = y < 13 ? stone : grass;

				const VoxelBox space{ { 0, 0, 0 }, { 40, 30, 40 } };
				Placement home = Placement::identityFor(block.box());

				VoxelRegion same(space);
				place(same, block, home, false);
				VoxelRegion expected(space);
				for (int y = 10; y < 16; ++y)
				for (int z = 10; z < 18; ++z)
				for (int x = 10; x < 22; ++x)
					expected.at(x, y, z) = block.at(x, y, z);
				check(std::memcmp(same.data(), expected.data(), same.volume()) == 0, "the identity placement puts a block back where it came from");

				// Four quarter turns about y, each one lifting the result of the last.
				VoxelRegion turning = block;
				Placement quarter;
				quarter.rotation_ = glm::angleAxis(glm::radians(90.f), glm::vec3{ 0.f, 1.f, 0.f });
				for (int turn = 0; turn < 4; ++turn)
				{
					quarter.centre_ = 0.5f * glm::vec3(turning.box().min_ + turning.box().max_);
					VoxelRegion next(placedBounds(turning.box(), quarter));
					place(next, turning, quarter, false);
					if (turn == 0)
						check(next.size() == glm::ivec3{ 8, 6, 12 } && solidCount(next) == solidCount(block), "a quarter turn swaps the block's width and depth and keeps every voxel");
					turning = std::move(next);
				}
				check(turning.box() == block.box() && std::memcmp(turning.data(), block.data(), block.volume()) == 0, "four quarter turns bring a block back exactly");

				// Merge Empty: the block's air leaves what is there alone.
				VoxelRegion filled(space, water);
				Placement moved = home;
				moved.centre_ += glm::vec3{ 5.f, 0.f, 0.f };
				place(filled, block, moved, true);
				check(filled.at(15, 14, 10) == grass && filled.at(25, 14, 16) == water, "with Merge Empty the block's air does not clear the target");
				VoxelRegion cleared(space, water);
				place(cleared, block, moved, false);
				check(cleared.at(25, 14, 16) == kAir && cleared.at(5, 5, 5) == water, "without it the block arrives with its own air, and only inside its box");

				Placement doubled = home;
				doubled.scale_ = glm::vec3{ 2.f };
				VoxelRegion big(VoxelBox{ { -10, -10, -10 }, { 50, 40, 50 } });
				place(big, block, doubled, false);
				check(solidCount(big) == 8 * solidCount(block), "scaling by two makes every voxel eight");
			}

			// --------------------- Batched LoD rebuild --------------------
			//
			// After an edit each ancestor is rebuilt once. It has to come out
			// the same as rebuilding the whole pyramid.
			{
				World edited = makeGround(stone);
				edited.rebuildLevels(materials);

				OpParams params;
				params.op_ = Op::Subtract;
				const Stamp stamp = sphereAt({ 50.f, 28.f, 50.f }, 20.f);
				VoxelRegion region;
				read(edited, contextBox(stamp, params), region);
				apply(region, stamp, params);
				const WriteResult result = write(edited, region);
				const auto rebuilt = edited.remipAncestors(result.changed_, materials);

				World reference(edited.dims());
				{
					VoxelRegion all;
					read(edited, wholeWorld(edited), all);
					write(reference, all);
				}
				reference.rebuildLevels(materials);

				bool same = true;
				for (int level = 1; level < kLodLevels; ++level)
				for (std::size_t i = 0; i < edited.sectionCount(level); ++i)
				{
					const glm::ivec3 c = edited.sectionCoord(level, i);
					same = same && edited.childMask(level, c.x, c.y, c.z) == reference.childMask(level, c.x, c.y, c.z);
					for (std::size_t v = 0; v < static_cast<std::size_t>(kSectionVolume) && same; ++v)
						same = edited.sectionAt(level, i).at(v) == reference.sectionAt(level, i).at(v);
				}
				check(same, "re-mipping the ancestors of the changed sections equals rebuilding every level");

				std::vector<std::pair<int, glm::ivec3>> unique = rebuilt;
				std::sort(unique.begin(), unique.end(), [](const auto& a, const auto& b)
				{
					if (a.first != b.first) return a.first < b.first;
					if (a.second.y != b.second.y) return a.second.y < b.second.y;
					if (a.second.z != b.second.z) return a.second.z < b.second.z;
					return a.second.x < b.second.x;
				});
				check(std::adjacent_find(unique.begin(), unique.end()) == unique.end(), "and rebuilds no ancestor twice");
			}
		}
		catch (const std::exception& e)
		{
			std::cerr << "[voxel-edit-check] threw: " << e.what() << "\n";
			++failures;
		}

		if (failures == 0)
			std::cout << "[voxel-edit-check] all checks passed\n";
		else
			std::cerr << "[voxel-edit-check] SOME CHECKS FAILED\n";
	}
}
