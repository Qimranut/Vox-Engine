/*************************************************************************************************************
\file       voxel_self_check.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Startup self-check for the voxel pipeline: quad packing, the mip rule, sections and worlds,
            the greedy mesher, the .voxmap codec, noise, erosion and lakes, recipes, biome terrain and
            end-to-end generation determinism.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "voxel_self_check.hpp"

// Voxel
#include "../../voxel/types/quad.hpp"
#include "../../voxel/types/material_table.hpp"
#include "../../voxel/world/section.hpp"
#include "../../voxel/world/world.hpp"
#include "../../voxel/world/mipper.hpp"
#include "../../voxel/mesh/section_mesher.hpp"
#include "../../voxel/io/voxmap_codec.hpp"
#include "../../voxel/io/voxmap_io.hpp"
#include "../../voxel/gen/erosion.hpp"
#include "../../voxel/gen/hydrology.hpp"
#include "../../voxel/gen/recipe.hpp"
#include "../../voxel/gen/terrain_generator.hpp"
#include "../../voxel/gen/preview/stage_preview.hpp"
#include "../../voxel/gen/biome/biome_library.hpp"
#include "../../voxel/gen/biome/biome_map.hpp"
#include "../../voxel/gen/biome/biome_terrain.hpp"
#include "../../voxel/gen/biome/height_ops.hpp"

// Noise
#include "../../util/noise/fbm.hpp"
#include "../../util/noise/simplex_noise.hpp"
#include "../../util/noise/spline.hpp"
#include "../../util/noise/prng.hpp"

// Threads
#include "../../util/thread/job_queue.hpp"

// stds
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <stdexcept>
#include <utility>


namespace engine::checks
{
	/*!
		@brief
			Exercises the voxel pipeline's pure pieces and prints "[voxel-check]"
			results. Never throws.
		*/
	void runVoxelSelfCheck()
	{
		using namespace voxel;

		int failures = 0;

		const auto check = [&failures](bool ok, const char* what)
		{
			if (!ok)
			{
				std::cerr << "[voxel-check] FAILED: " << what << "\n";
				++failures;
			}
		};

		const auto nearly = [](double a, double b, double eps)
		{
			return std::fabs(a - b) < eps;
		};

		const MaterialTable materials = MaterialTable::builtin();
		const MaterialId stone = materials.id("stone");
		const MaterialId dirt = materials.id("dirt");
		const MaterialId grass = materials.id("grass");

		// ---------------------- Quad packing ------------------------
		//
		// The GPU unpacks these bits with fixed shifts. A field that
		// overlaps its neighbour corrupts positions silently, on the far
		// side of the world.
		{
			bool ok = true;
			for (std::uint8_t face = 0; face < 6 && ok; ++face)
			for (int size = 1; size <= 16 && ok; size += 15)
			for (int coord = 0; coord <= 31 && ok; coord += 31)
			{
				const Quad q = Quad::pack(face, size, 17 - size, coord, 31 - coord, coord, 0xFFFF, 0xA5);
				ok = q.face() == face && q.length() == size && q.width() == 17 - size
					&& q.x() == coord && q.y() == 31 - coord && q.z() == coord
					&& q.material() == 0xFFFF && q.ao() == 0xA5
					&& ((static_cast<std::uint64_t>(q.hi()) << 32) | q.lo()) == q.bits_;
			}
			check(ok, "quad pack/unpack does not round-trip");

			const Quad z = Quad::pack(0, 1, 1, 0, 0, 0, 0, 0);
			check(z.bits_ == 0, "an all-zero quad is not zero bits");

			check(unswizzle(0, 3, 5, 7) == glm::ivec3{ 3, 7, 5 }
				&& unswizzle(1, 3, 5, 7) == glm::ivec3{ 3, 5, 7 }
				&& unswizzle(2, 3, 5, 7) == glm::ivec3{ 7, 3, 5 },
				"(u, v, d) swizzle disagrees with the documented axis frames");
		}

		// ----------------------- Mip rule ---------------------------
		//
		// Grass(3) must beat dirt(2) beats stone(1); on a tie the +++
		// corner wins so LoD surfaces bias upward and outward, and air
		// never wins over any solid.
		{
			MaterialId c[8] = { kAir, kAir, kAir, kAir, kAir, kAir, kAir, kAir };
			check(mip8(c, materials) == kAir, "eight air voxels did not mip to air");

			c[0] = grass; c[3] = dirt;
			check(mip8(c, materials) == grass, "higher LoD priority did not win the mip");

			c[0] = grass; c[7] = grass; c[3] = kAir;
			check(mip8(c, materials) == grass, "tie between equal materials lost the material");

			MaterialId t[8] = { kAir, kAir, kAir, kAir, kAir, kAir, kAir, kAir };
			t[2] = dirt; t[7] = dirt;
			// Same material, so the winner is indistinguishable - test the
			// tie-break through two materials of equal priority instead.
			MaterialTable tie = MaterialTable::builtin();
			Material extra;
			extra.name_ = "moss";
			extra.lodPriority_ = materials.get(grass).lodPriority_;
			const MaterialId moss = tie.add(std::move(extra));
			MaterialId u[8] = { kAir, kAir, kAir, kAir, kAir, kAir, kAir, kAir };
			u[1] = moss; u[7] = grass;
			check(mip8(u, tie) == grass, "equal priorities did not resolve to the +++ corner");
			u[1] = grass; u[7] = moss;
			check(mip8(u, tie) == moss, "equal priorities did not resolve to the +++ corner (reversed)");

			// Only air can lose to a solid of priority 0.
			MaterialTable zero = MaterialTable::builtin();
			Material weak;
			weak.name_ = "ash";
			weak.lodPriority_ = 0;
			const MaterialId ash = zero.add(std::move(weak));
			MaterialId w[8] = { ash, kAir, kAir, kAir, kAir, kAir, kAir, kAir };
			check(mip8(w, zero) == ash, "a priority-0 solid lost to air");
		}

		// --------------------- Section + World ----------------------
		{
			Section s = Section::uniform(stone);
			check(s.isUniform() && s.get(5, 6, 7) == stone, "uniform section does not report its material");
			check(!s.set(5, 6, 7, stone), "writing the uniform value promoted the section");
			check(s.set(5, 6, 7, dirt) && !s.isUniform() && s.get(5, 6, 7) == dirt && s.get(0, 0, 0) == stone,
				"promoting a uniform section lost the fill value");
			check(s.set(5, 6, 7, stone) && s.tryCollapse() && s.isUniform() && s.uniformMaterial() == stone,
				"a dense section of one value did not collapse back to uniform");

			World w(WorldDims::fromVoxels({ 100, 40, 70 }, 0.1f, { 0.f, 0.f, 0.f }));
			check(w.sectionsAt(0) == glm::ivec3{ 4, 2, 3 }, "section count is not the ceiling of voxels / 32");
			check(w.sectionsAt(1) == glm::ivec3{ 2, 1, 2 } && w.sectionsAt(2) == glm::ivec3{ 1, 1, 1 },
				"coarser levels are not the ceiling of the finer level / 2");
			check(w.get(99, 39, 69) == kAir && w.get(100, 0, 0) == kAir, "out-of-extent voxels are not air");
			check(w.set(33, 1, 2, grass) && w.get(33, 1, 2) == grass && !w.set(100, 0, 0, stone),
				"level-0 set/get disagree");

			const auto dirty = w.takeDirtyLevel0();
			check(dirty.size() == 1 && dirty[0] == glm::ivec3{ 1, 0, 0 }, "an edit did not dirty exactly its section");

			w.editSection(0, 0, 0, 0).fill(stone);
			w.rebuildLevels(materials);
			check(w.section(1, 0, 0, 0).get(0, 0, 0) == stone, "a full child did not mip into its parent's corner");
			check(w.section(1, 0, 0, 0).get(31, 31, 31) == kAir, "an empty child leaked material into the parent");
			check(w.childMask(1, 0, 0, 0) == ((1u << childIndex(0, 0, 0)) | (1u << childIndex(1, 0, 0))),
				"child mask does not mark exactly the non-empty children");
			check(w.section(2, 0, 0, 0).get(0, 0, 0) == stone && w.section(3, 0, 0, 0).get(0, 0, 0) == stone,
				"mip did not propagate to the coarsest level");

			// The single grass voxel at (33,1,2) sits in child (1,0,0) of the
			// level-1 root; at level 1 it lands at (16, 0, 1).
			check(w.section(1, 0, 0, 0).get(16, 0, 1) == grass, "a lone voxel vanished when mipped");
		}

		// ------------------------- Mesher ---------------------------
		//
		// Wrong face ownership or a broken bucket boundary renders as
		// missing or doubled walls, which is easy to blame on the shader.
		{
			const auto quadsOf = [](const SectionMesh& m, int face)
			{
				std::vector<Quad> out;
				const std::uint32_t o = m.bucketOffset(face);
				for (std::uint32_t i = 0; i < m.counts_[face]; ++i)
					out.push_back(m.quads_[o + i]);
				return out;
			};

			// A lone 2x2x2 cube: six quads, each 2x2, one per direction.
			{
				Section s = Section::dense();
				for (int x = 10; x < 12; ++x)
				for (int y = 10; y < 12; ++y)
				for (int z = 10; z < 12; ++z)
					s.set(x, y, z, stone);

				MeshInput in;
				in.self_ = &s;
				const SectionMesh m = meshSection(in, materials, false);

				bool ok = m.quadCount() == 6;
				for (int f = 0; f < FaceCount && ok; ++f)
				{
					const auto q = quadsOf(m, f);
					ok = q.size() == 1 && q[0].length() == 2 && q[0].width() == 2 && q[0].face() == f
						&& q[0].material() == stone && q[0].ao() == 0xFF;
					if (ok)
					{
						// The owning voxel sits on the cube's face plane.
						const int d = q[0].d();
						ok = d == (faceSide(f) ? 11 : 10) && q[0].u() == 10 && q[0].v() == 10;
					}
				}
				check(ok, "a 2x2x2 cube did not mesh into six 2x2 quads with the right owners");

				// The cube's AABB covers voxels 10..11 on every axis.
				check(m.aabb_ == (10u | 10u << 5 | 10u << 10 | 1u << 15 | 1u << 20 | 1u << 25),
					"section AABB does not cover the cube");
			}

			// A full uniform section with air around: 6 faces x 4 tiles of 16x16.
			{
				const Section s = Section::uniform(stone);
				MeshInput in;
				in.self_ = &s;
				const SectionMesh m = meshSection(in, materials, false);

				bool ok = m.quadCount() == 24;
				for (int f = 0; f < FaceCount && ok; ++f)
				{
					ok = m.counts_[f] == 4;
					for (const Quad& q : quadsOf(m, f))
						ok = ok && q.length() == 16 && q.width() == 16;
				}
				check(ok, "a full section did not mesh into 24 quads of 16x16");
			}

			// Fully enclosed: nothing to draw.
			{
				const Section s = Section::uniform(stone);
				MeshInput in;
				in.self_ = &s;
				for (int f = 0; f < FaceCount; ++f)
					in.neighbours_[f] = &s;
				check(meshSection(in, materials, false).empty(), "a buried section produced faces");

				// Sealed (coarse LoD): the same section keeps its six walls,
				// which is what closes the crack beside a finer node.
				in.sealBorders_ = true;
				check(meshSection(in, materials, false).quadCount() == 24, "a sealed buried section did not keep its 24 border quads");
				check(meshSection(in, materials, true).quadCount() == 24, "AO split the border quads of a sealed section");
			}

			// Materials never merge across each other.
			{
				Section s = Section::dense();
				for (int x = 0; x < 32; ++x)
				for (int z = 0; z < 32; ++z)
					s.set(x, 0, z, x < 16 ? grass : dirt);

				MeshInput in;
				in.self_ = &s;
				const SectionMesh m = meshSection(in, materials, false);
				const auto top = quadsOf(m, FacePosY);
				bool ok = top.size() == 4;
				for (const Quad& q : top)
					ok = ok && q.length() == 16 && q.width() == 16 && (q.x() < 16 ? q.material() == grass : q.material() == dirt);
				check(ok, "different materials merged into one quad, or same materials did not merge");
			}

			// Checkerboard: the worst case must still fit the 16-bit bucket counts.
			{
				Section s = Section::dense();
				for (int x = 0; x < 32; ++x)
				for (int y = 0; y < 32; ++y)
				for (int z = 0; z < 32; ++z)
					if (((x + y + z) & 1) == 0)
						s.set(x, y, z, stone);

				MeshInput in;
				in.self_ = &s;
				const SectionMesh m = meshSection(in, materials, false);
				bool ok = m.quadCount() == 6 * 16384;
				for (int f = 0; f < FaceCount; ++f)
					ok = ok && m.counts_[f] == 16384;
				check(ok, "checkerboard bucket counts are wrong - a 16-bit count overflowed or faces were lost");
			}

			// AO: a floor with one block on it darkens the floor corners next
			// to the block, and the block's top stays fully open.
			{
				Section s = Section::dense();
				for (int x = 0; x < 32; ++x)
				for (int z = 0; z < 32; ++z)
					s.set(x, 0, z, stone);
				s.set(8, 1, 8, stone);

				MeshInput in;
				in.self_ = &s;
				const SectionMesh m = meshSection(in, materials, true);

				bool sawDark = false, topOpen = false;
				for (const Quad& q : quadsOf(m, FacePosY))
				{
					if (q.y() == 1) topOpen = q.ao() == 0xFF;
					else if (q.ao() != 0xFF) sawDark = true;
				}
				check(sawDark && topOpen, "corner AO did not darken floor cells beside a block");
			}
		}

		// ---------------------- Map codec ---------------------------
		//
		// A codec that drops a bit reads back as a map with the wrong
		// materials in a few sections, which looks like a generator bug.
		{
			using namespace voxel::voxmap;
			util::noise::Pcg32 rng(2024);

			const auto identity = [](std::uint16_t id) { return static_cast<MaterialId>(id); };

			for (const int paletteSize : { 1, 2, 3, 9, 40 })
			{
				std::vector<std::uint8_t> values(kSectionVolume);
				for (int i = 0; i < kSectionVolume; ++i)
					values[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(rng.nextBelow(static_cast<std::uint32_t>(paletteSize)));

				std::vector<unsigned char> payload;
				const std::uint8_t codec = encodeSection(values.data(), payload);

				std::vector<std::uint8_t> back(kSectionVolume, 0xEE);
				const bool ok = decodeSection(payload, codec, identity, back.data()) && back == values;
				check(ok, "palette codec does not round-trip a random section");
			}

			// Runs compress with RLE; a random section must not.
			{
				std::vector<std::uint8_t> runs(kSectionVolume, 0);
				for (int i = 0; i < kSectionVolume; ++i)
					runs[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((i / 700) % 3 + 1);
				std::vector<unsigned char> payload;
				const std::uint8_t codec = encodeSection(runs.data(), payload);
				check((codec & 0x10) != 0 && payload.size() < 1024, "runs did not choose RLE");

				std::vector<std::uint8_t> back(kSectionVolume, 0);
				check(decodeSection(payload, codec, identity, back.data()) && back == runs, "RLE payload does not round-trip");
			}

			// Whole-file round trip with a renamed material -> exactly one warning.
			{
				World w(WorldDims::fromVoxels({ 70, 40, 33 }, 0.1f, { 1.f, 2.f, 3.f }));
				w.editSection(0, 0, 0, 0).fill(stone);
				w.set(40, 5, 6, grass);
				w.set(41, 5, 6, dirt);
				w.set(3, 33, 1, dirt);

				Info info;
				info.seed_ = 777;
				info.recipePath_ = "maps/test.voxgen";
				const std::vector<unsigned char> bytes = write(w, materials, info);

				ReadResult rr = read(bytes, materials);
				bool ok = rr.world_ != nullptr && rr.warnings_.empty() && rr.info_.seed_ == 777 && rr.info_.recipePath_ == "maps/test.voxgen";
				ok = ok && rr.world_->dims().voxels_ == glm::ivec3{ 70, 40, 33 } && rr.world_->dims().origin_ == glm::vec3{ 1.f, 2.f, 3.f };
				ok = ok && rr.world_->section(0, 0, 0, 0).isUniform() && rr.world_->section(0, 0, 0, 0).uniformMaterial() == stone;
				ok = ok && rr.world_->get(40, 5, 6) == grass && rr.world_->get(41, 5, 6) == dirt && rr.world_->get(3, 33, 1) == dirt;
				ok = ok && rr.world_->get(42, 5, 6) == kAir;
				check(ok, ".voxmap does not round-trip");

				// Same input, same bytes: what lets a recipe stand in for the file.
				check(write(w, materials, info) == bytes, ".voxmap writer is not deterministic");

				// Load against a table where "grass" was renamed.
				MaterialTable renamed;
				Material air; air.name_ = "air"; air.opaque_ = false; renamed.add(std::move(air));
				Material st; st.name_ = "stone"; renamed.add(std::move(st));
				Material di; di.name_ = "dirt"; renamed.add(std::move(di));
				Material tu; tu.name_ = "turf"; renamed.add(std::move(tu));
				for (const char* kept : { "sand", "snow", "water", "ice", "sandstone", "redrock", "mud" })
				{
					Material same;
					same.name_ = kept;
					renamed.add(std::move(same));
				}
				ReadResult rr2 = read(bytes, renamed);
				check(rr2.warnings_.size() == 2 && rr2.world_->get(40, 5, 6) == 1, "renamed material did not warn and fall back");

				// Truncation is detected.
				bool threw = false;
				try { read(std::span<const unsigned char>(bytes.data(), bytes.size() - 8), materials); }
				catch (const std::exception&) { threw = true; }
				check(threw, "a truncated .voxmap was accepted");
			}
		}

		// -------------------- Noise determinism ---------------------
		//
		// Generation runs on many threads in any order. If noise depended
		// on anything but (seed, coords), two runs would diverge and a map
		// would not match its recipe.
		{
			using namespace util::noise;
			const FbmParams p{ 4, 0.05, 2.0, 0.5 };

			const double a = fbm2(42, 12.5, -3.25, p);
			const double b = fbm2(42, 12.5, -3.25, p);
			const double c = fbm2(43, 12.5, -3.25, p);
			check(a == b, "fbm2 is not deterministic for a seed");
			check(a != c, "fbm2 ignores the seed");
			check(std::fabs(a) <= 1.0001, "fbm2 is not normalised into [-1, 1]");

			const double d = fbm3(7, 1.5, 2.5, -8.0, p);
			check(d == fbm3(7, 1.5, 2.5, -8.0, p) && std::fabs(d) <= 1.0001, "fbm3 is not deterministic or normalised");

			bool ridgedOk = true;
			double minR = 1.0, maxR = 0.0;
			for (int i = 0; i < 200 && ridgedOk; ++i)
			{
				const double r = ridged2(99, i * 3.7, i * -1.3, p);
				ridgedOk = r >= 0.0 && r <= 1.0;
				minR = std::min(minR, r);
				maxR = std::max(maxR, r);
			}
			check(ridgedOk, "ridged2 left [0, 1]");
			check(maxR - minR > 0.2, "ridged2 is flat");

			double wx, wy;
			domainWarp2(5, 10.0, 20.0, 30.0, p, wx, wy);
			check(wx != 10.0 || wy != 20.0, "domain warp did nothing");

			Pcg32 g1(1234), g2(1234);
			check(g1.next() == g2.next() && g1.nextFloat() == g2.nextFloat(), "PCG32 streams differ for one seed");
			bool inRange = true;
			for (int i = 0; i < 1000 && inRange; ++i)
			{
				const float f = g1.nextFloat();
				inRange = f >= 0.f && f < 1.f && g2.nextBelow(10) < 10;
			}
			check(inRange, "PCG32 float or bounded int left its range");

			// The analytic gradient drives slope damping; a wrong sign or
			// scale would damp the flats and roughen the cliffs.
			bool derivOk = true;
			for (int i = 0; i < 50 && derivOk; ++i)
			{
				const double x = i * 0.37 - 4.0, y = i * -0.53 + 2.0, e = 1e-5;
				double dx, dy;
				const double v = noise2Deriv(11, x, y, dx, dy);
				const double fx = (noise2(11, x + e, y) - noise2(11, x - e, y)) / (2.0 * e);
				const double fy = (noise2(11, x, y + e) - noise2(11, x, y - e)) / (2.0 * e);
				derivOk = v == noise2(11, x, y) && nearly(dx, fx, 1e-4) && nearly(dy, fy, 1e-4);
			}
			check(derivOk, "noise2Deriv disagrees with noise2 or with its finite difference");
			check(nearly(dampedFbm2(42, 12.5, -3.25, p, 0.0), a, 1e-12), "dampedFbm2 without damping is not fbm2");

			bool smoothOk = true;
			for (int i = 0; i < 200 && smoothOk; ++i)
			{
				const double r = ridgedSmooth2(99, i * 3.7, i * -1.3, p);
				smoothOk = r >= 0.0 && r <= 1.0;
			}
			check(smoothOk, "ridgedSmooth2 left [0, 1]");
		}

		// --------------------- Terrain prepass ----------------------
		//
		// The erosion passes reshape the whole map, so a mistake here is
		// not a wrong voxel but a wrong landscape - and one that differs
		// between two machines if any pass depends on thread timing.
		{
			Recipe::Erosion erosion;
			erosion.droplets_ = 4000;
			erosion.thermalIterations_ = 400;
			erosion.rockTalusDeg_ = 45.f;

			// Thermal: a lone 5 m pillar must slump to the talus angle
			// without creating or destroying material.
			{
				HeightField field(33, 33, 0.2f, 1.f), loose(33, 33, 0.2f, 0.f);
				field.at(16, 16) = 6.f;
				double before = 0.0;
				for (const float v : field.h_) before += v;

				erodeThermal(field, loose, erosion);

				double after = 0.0;
				float steepest = 0.f;
				for (const float v : field.h_) after += v;
				for (int z = 0; z < 33; ++z)
				for (int x = 0; x < 32; ++x)
					steepest = std::max({ steepest, std::fabs(field.at(x, z) - field.at(x + 1, z)), std::fabs(field.at(z, x) - field.at(z, x + 1)) });

				check(nearly(before, after, 1e-2), "thermal erosion did not conserve material");
				check(steepest <= 0.2f * 1.15f, "thermal erosion left a slope well above the talus angle");
				check(field.at(16, 16) < 2.f, "thermal erosion left the pillar standing");
			}

			// Droplets: same inputs, same result, and it must do something.
			{
				const auto run = [&erosion]
				{
					HeightField field(64, 64, 0.2f), loose(64, 64, 0.2f, 0.f);
					for (int z = 0; z < 64; ++z)
					for (int x = 0; x < 64; ++x)
						field.at(x, z) = 4.f + 3.f * static_cast<float>(util::noise::fbm2(5, x, z, util::noise::FbmParams{ 4, 0.05, 2.0, 0.5 }));
					const HeightField original = field;
					erodeHydraulic(field, loose, erosion, 77);
					return std::pair{ field.h_, field.h_ != original.h_ };
				};

				const auto first = run(), second = run();
				check(first.second, "hydraulic erosion changed nothing");
				check(first.first == second.first, "hydraulic erosion is not deterministic");

				bool finite = true;
				for (const float v : first.first) finite = finite && std::isfinite(v);
				check(finite, "hydraulic erosion produced a non-finite height");
			}

			// Lakes: a 4x4 pit two metres deep in a plateau, and a dimple.
			{
				HeightField field(16, 16, 1.f, 10.f);
				for (int z = 6; z < 10; ++z)
				for (int x = 6; x < 10; ++x)
					field.at(x, z) = 8.f;
				field.at(2, 2) = 9.9f;

				const HeightField filled = fillDepressions(field);
				check(nearly(filled.at(7, 7), 10.f, 1e-6) && nearly(filled.at(0, 0), 10.f, 1e-6), "priority-flood did not fill a pit to its rim");

				Recipe::Water water;
				water.enabled_ = true;
				water.minDepth_ = 0.4f;
				water.minArea_ = 4.f;
				water.maxDepth_ = 10.f;
				water.fillFraction_ = 0.5f;
				const HeightField lakes = findLakes(field, water);

				check(nearly(lakes.at(7, 7), 9.f, 1e-5) && lakes.at(0, 0) == kNoWater && lakes.at(5, 7) == kNoWater,
					"a lake did not fill to the requested share of its depth");
				check(nearly(field.at(2, 2), 10.f, 1e-6) && lakes.at(2, 2) == kNoWater, "a dimple was not levelled off as an erosion pit");
			}

			// End to end: the world must not depend on the thread count, and
			// no column may stand alone above its neighbours.
			try
			{
				const std::string json = R"({
					"version": 2, "seed": 9, "voxelSize": 0.1, "size": { "x": 96, "y": 64, "z": 96 },
					"shape": { "baseHeight": 1.5,
						"warp": { "wavelength": 6, "amplitude": 0.8, "octaves": 2 },
						"massif": { "wavelength": 8, "amplitude": 2.5, "octaves": 3 },
						"detail": { "wavelength": 3, "amplitude": 0.5, "octaves": 4 },
						"ring": { "inner": 0.4, "outer": 0.95 },
						"basins": [ { "centre": [0.5, 0.5], "radius": 2.0, "depth": 0.8 } ] },
					"erosion": { "droplets": 3000, "thermal": { "iterations": 20 } },
					"water": { "enabled": true, "minArea": 0.5, "minDepth": 0.2 },
					"surface": { "snowline": 3.5, "sandMaxHeight": 2.0 }
				})";
				const Recipe recipe = Recipe::parse(std::span(reinterpret_cast<const unsigned char*>(json.data()), json.size()));
				const TerrainGenerator generator(recipe, materials);

				World serial(generator.dims()), threaded(generator.dims());
				std::atomic<int> units{ 0 };
				generator.generate(serial, nullptr, &units);
				{
					util::thread::JobQueue jobs(3);
					generator.generate(threaded, &jobs);
				}

				voxmap::Info info;
				check(voxmap::write(serial, materials, info) == voxmap::write(threaded, materials, info),
					"generated terrain depends on the thread count");
				check(units.load() == generator.workUnits(), "generate() did not report workUnits() progress ticks");

				const std::optional<TerrainMaps> maps = generator.buildMaps();
				bool spike = false, solid = true;
				for (int z = 1; z < 95 && maps; ++z)
				for (int x = 1; x < 95; ++x)
				{
					const int h = maps->surface_[maps->index(x, z)];
					const int tallest = std::max({ maps->surface_[maps->index(x - 1, z)], maps->surface_[maps->index(x + 1, z)],
												   maps->surface_[maps->index(x, z - 1)], maps->surface_[maps->index(x, z + 1)] });
					spike = spike || h - tallest >= 3;
					solid = solid && serial.get(x, h, z) != kAir && serial.get(x, std::max<int>(h, maps->waterTop_[maps->index(x, z)]) + 1, z) == kAir;
				}
				check(maps.has_value() && !spike, "the prepass left a spike column");
				check(solid, "the filled world disagrees with the terrain maps");

				// A pipeline view watches the stages go by. It must see every
				// one, in order, and the terrain must not know it was watched.
				preview::CapturingObserver watched(materials.id("water"), 64);
				const std::optional<TerrainMaps> seen = generator.buildMaps(nullptr, nullptr, nullptr, &watched);
				check(seen && maps && seen->surface_ == maps->surface_ && seen->topMaterial_ == maps->topMaterial_ && seen->waterTop_ == maps->waterTop_,
					"watching the prepass changed the terrain");

				const std::vector<preview::StageCapture>& stages = watched.stages();
				bool ordered = stages.size() == static_cast<std::size_t>(GenStage::Count);
				for (std::size_t i = 0; i < stages.size() && ordered; ++i)
					ordered = stages[i].stage_ == static_cast<GenStage>(i) && stages[i].height_.size() != 0;
				check(ordered, "the observer did not see every stage, in order");

				if (ordered)
				{
					check(stages[1].movedVolume_ > 0. && stages[0].movedVolume_ == 0., "hydraulic erosion was reported as moving nothing");
					check(stages.back().material_.size() == stages.back().height_.size() && stages[0].material_.empty(),
						"only the last stage has materials");
					check(std::max(stages.back().height_.width_, stages.back().height_.depth_) <= 64, "a stage capture was not thinned to the size asked for");

					preview::RenderOptions options;
					const preview::Image shaded = preview::render(stages.back(), nullptr, materials, options);
					check(shaded.width_ == stages.back().height_.width_ && shaded.rgba_.size() == static_cast<std::size_t>(shaded.width_) * shaded.height_ * 4,
						"a stage picture has the size of its capture");

					options.mode_ = preview::Mode::Change;
					check(!preview::supports(stages[0], nullptr, preview::Mode::Change) && preview::supports(stages[1], &stages[0], preview::Mode::Change),
						"Change needs a stage before it");
					check(preview::supports(stages[3], &stages[2], preview::Mode::Water) && !preview::supports(stages[2], &stages[1], preview::Mode::Water),
						"Water appears with the Lakes stage");
				}

				bool rejected = false;
				const std::string old = R"({ "version": 1 })";
				try { Recipe::parse(std::span(reinterpret_cast<const unsigned char*>(old.data()), old.size())); }
				catch (const std::exception&) { rejected = true; }
				check(rejected, "a version 1 recipe was accepted");
			}
			catch (const std::exception& e)
			{
				std::cerr << "[voxel-check] terrain generation threw: " << e.what() << "\n";
				++failures;
			}
		}

		// ------------------------- Biomes ---------------------------
		//
		// Biome terrain is laid out in world space and travels as a recipe
		// that must reproduce it exactly. Both promises fail silently: a
		// region that shows different ground after being moved, a level whose
		// saved recipe regenerates something else on a teammate's machine.
		{
			using namespace util::noise;

			// Shaping functions.
			{
				bool profileOk = true, terraceOk = true;
				double last = terrace(-7.0, 1.5, 6.0);
				for (int i = 0; i <= 2000; ++i)
				{
					const double t = -3.0 + 6.0 * i / 2000.0;
					const double d = duneProfile(t, 0.7);
					profileOk = profileOk && d >= 0.0 && d <= 1.0 && nearly(d, duneProfile(t + 1.0, 0.7), 1e-9);

					const double h = -7.0 + 14.0 * i / 2000.0;
					const double stepped = terrace(h, 1.5, 6.0);
					terraceOk = terraceOk && stepped >= last - 1e-12 && stepped - last < 0.08 && std::fabs(stepped - h) <= 1.5;
					last = stepped;
				}
				check(profileOk, "duneProfile left [0, 1] or is not periodic");
				check(nearly(duneProfile(0.0, 0.7), 0.0, 1e-12) && nearly(duneProfile(0.7, 0.7), 1.0, 1e-12), "duneProfile trough or crest is misplaced");
				check(terraceOk, "terrace is not monotonic, jumps, or strays more than a step");
				check(nearly(terrace(3.0, 1.5, 6.0), 3.0, 1e-12), "terrace moves a height that sits on a tread boundary");
				check(smoothAbs(0.0) > 0.0 && nearly(smoothAbs(-0.8), smoothAbs(0.8), 1e-15), "smoothAbs is not even or has a corner");

				bool billowOk = true;
				FbmParams p;
				p.frequency_ = 0.07;
				for (int i = 0; i < 500; ++i)
				{
					const double b = billow2(5, i * 0.61, i * -0.37, p);
					billowOk = billowOk && b >= -1.0 && b <= 1.0;
				}
				check(billowOk, "billow2 left [-1, 1]");
			}

			// Every registered height op compiles with its defaults and stays
			// inside the bounds it declares.
			for (const HeightOp& op : heightOps())
			{
				HeightLayer layer;
				layer.op_ = op.name_;
				layer.wavelength_ = 12.0;
				layer.amplitude_ = 3.0;
				layer.warp_ = { 9.0, 2.0 };
				layer.mask_ = { 20.0, 0.25 };

				bool inside = true;
				try
				{
					const CompiledLayer compiled(layer, 77, 0.8);
					for (int i = 0; i < 600; ++i)
					{
						const double v = compiled.sample(i * 0.83 - 200.0, i * -1.31 + 90.0);
						inside = inside && v >= compiled.low() - 1e-9 && v <= compiled.high() + 1e-9;
					}
				}
				catch (const std::exception&)
				{
					inside = false;
				}
				check(inside, "a height op left its declared bounds or did not compile");
			}
			{
				bool rejected = false;
				HeightLayer bad;
				bad.params_["windDeg"] = 10.0;		// fbm takes no such parameter
				try { CompiledLayer(bad, 1, 0.8); }
				catch (const std::exception&) { rejected = true; }
				check(rejected, "a height op accepted a parameter it does not declare");
			}

			// The biome map.
			{
				const BiomeMap map(4242, 5, 20.0, 0.5, 0.8);
				const BiomeMap hard(4242, 5, 20.0, 0.0, 0.8);

				bool normalised = true, smooth = true, same = true;
				bool seen[5] = {};
				std::size_t blended = 0, blendedHard = 0;
				BiomeSample before = map.sample(-60.0, -60.0);

				for (int i = 0; i < 2400; ++i)
				{
					const double x = -60.0 + i * 0.05, z = -60.0 + i * 0.031;
					const BiomeSample s = map.sample(x, z);

					float total = 0.f;
					float jump = 0.f;
					for (std::size_t b = 0; b < 5; ++b)
					{
						total += s.weight_[b];
						jump = std::max(jump, std::fabs(s.weight_[b] - before.weight_[b]));
					}
					normalised = normalised && nearly(total, 1.0, 1e-5) && s.weight_[static_cast<std::size_t>(s.nearest_)] > 0.f;
					smooth = smooth && jump < 0.05f;
					before = s;

					const BiomeSample again = map.sample(x, z);
					same = same && again.weight_ == s.weight_ && again.nearest_ == s.nearest_;

					blended += s.weight_[static_cast<std::size_t>(s.nearest_)] < 0.999f ? 1 : 0;
					const BiomeSample h = hard.sample(x, z);
					blendedHard += h.weight_[static_cast<std::size_t>(h.nearest_)] < 0.999f ? 1 : 0;
				}

				// Any 6 x 6 cells must show every biome.
				for (int z = 0; z < 120; z += 4)
				for (int x = 0; x < 120; x += 4)
					seen[map.sample(x, z).nearest_] = true;

				check(normalised, "biome weights do not sum to 1");
				check(smooth, "biome weights jump between neighbouring points");
				check(same, "the biome map is not a pure function of the point");
				check(blendedHard < blended, "blending does not widen the cross-fade");
				check(seen[0] && seen[1] && seen[2] && seen[3] && seen[4], "a chosen biome never appears on a small map");
				check(nearly(BiomeMap(1, 1, 20.0, 0.3, 0.8).sample(3.0, 4.0).weight_[0], 1.0, 1e-9), "a single biome does not cover the map");
			}

			try
			{
				const std::string json = R"({
					"version": 1,
					"generate": { "dropletsPerSquareMetre": 40, "shellDepth": 3.0 },
					"defaults": {
						"erosion": { "minRelief": 6.0, "thermal": { "iterations": 12 } },
						"water": { "enabled": true, "minArea": 0.5, "minDepth": 0.2, "maxDepth": 1.0 },
						"surface": { "base": "stone" }
					},
					"biomes": [
						{ "name": "flat", "colour": [10, 200, 30], "base": 0.0, "erodibility": 0.4, "talusDeg": 45,
						  "layers": [ { "op": "fbm", "wavelength": 9, "amplitude": 0.6, "octaves": 3 } ] },
						{ "name": "drift", "colour": [220, 200, 140], "featureSize": 20, "base": 0.5, "erodibility": 0.05, "talusDeg": 34,
						  "layers": [ { "op": "dunes", "wavelength": 4, "amplitude": 0.8, "windDeg": 30, "asymmetry": 0.7,
										"warp": { "wavelength": 9, "amplitude": 1.5 }, "mask": { "wavelength": 12, "floor": 0.4 } } ],
						  "palette": { "top": "sand", "under": "sand", "rock": "sandstone", "sand": "sand", "snow": "snow" } },
						{ "name": "mesa", "colour": [180, 100, 60], "base": 0.5, "edgeRelief": 0.3, "erodibility": 0.2, "talusDeg": 78,
						  "layers": [ { "op": "plateau", "wavelength": 8, "amplitude": 3, "octaves": 2 } ],
						  "terrace": { "step": 1.0, "sharpness": 5 },
						  "palette": { "top": "sandstone", "under": "sandstone", "rock": "redrock", "sand": "sand", "snow": "snow" },
						  "strata": { "wobble": 0.4, "bands": [ { "material": "redrock", "thickness": 0.5 }, { "material": "sandstone", "thickness": 0.3 } ] },
						  "rules": { "soilDepth": 0.3 } }
					]
				})";
				const BiomeLibrary library = BiomeLibrary::parse(std::span(reinterpret_cast<const unsigned char*>(json.data()), json.size()));

				GenerateSettings settings;
				settings.size_ = { 96, 128, 96 };
				settings.origin_ = { 0.f, 0.f, 0.f };
				settings.biomes_ = { "mesa", "flat", "drift" };
				settings.biomeSize_ = 10.f;
				settings.blending_ = 0.4f;
				settings.seed_ = 31;
				const Recipe recipe = composeBiomeRecipe(library, settings);

				check(recipe.biomes_.entries_.size() == 3 && recipe.biomes_.entries_[0].name_ == "flat" && recipe.biomes_.entries_[2].name_ == "mesa",
					"a composed recipe does not list its biomes in library order");

				// The recipe text must reproduce the recipe, and be stable.
				const auto reparse = [](const std::string& text)
				{
					return Recipe::parse(std::span(reinterpret_cast<const unsigned char*>(text.data()), text.size()));
				};
				const std::string text = recipe.toJson();
				check(reparse(text) == recipe, "a biome recipe does not survive toJson() and parse()");
				check(reparse(text).toJson() == text, "recipe text changes when it is written twice");

				// The same ground wherever the map's window is: a second map
				// shifted by whole coarse cells sees bit-equal landform.
				{
					Recipe moved = recipe;
					const int cellVoxels = recipe.erosion_.cellVoxels_;
					const int shiftX = 7, shiftZ = -5;		// coarse cells
					moved.origin_.x += static_cast<float>(shiftX * cellVoxels) * recipe.voxelSize_;
					moved.origin_.z += static_cast<float>(shiftZ * cellVoxels) * recipe.voxelSize_;

					const BiomeFields a = BiomeTerrain(recipe).build(40, 40, nullptr);
					const BiomeFields b = BiomeTerrain(moved).build(40, 40, nullptr);

					bool equal = true, varied = false;
					for (int z = 5; z < 40; ++z)
					for (int x = 0; x < 33; ++x)
					{
						equal = equal && a.height_.at(x + shiftX, z + shiftZ) == b.height_.at(x, z)
							&& a.talus_.at(x + shiftX, z + shiftZ) == b.talus_.at(x, z);
						varied = varied || a.height_.at(x, z) != a.height_.at(0, 5);
					}
					check(equal, "moving a biome map's origin changes the ground under it");
					check(varied, "the biome landform is flat");
				}

				// End to end, as for shaped terrain.
				const MaterialId redrock = materials.id("redrock"), sandstone = materials.id("sandstone");
				const TerrainGenerator generator(recipe, materials);
				World serial(generator.dims()), threaded(generator.dims());
				std::atomic<int> units{ 0 };
				generator.generate(serial, nullptr, &units);
				{
					util::thread::JobQueue jobs(3);
					generator.generate(threaded, &jobs);
				}

				voxmap::Info info;
				check(voxmap::write(serial, materials, info) == voxmap::write(threaded, materials, info),
					"biome terrain depends on the thread count");
				check(units.load() == generator.workUnits(), "biome generate() did not report workUnits() progress ticks");

				const std::optional<TerrainMaps> maps = generator.buildMaps();
				bool solid = maps.has_value() && maps->biome_.size() == maps->surface_.size();
				bool deepBase = true;
				bool seen[3] = {};
				for (int z = 1; z < 95 && solid; ++z)
				for (int x = 1; x < 95; ++x)
				{
					const std::size_t i = maps->index(x, z);
					const int h = maps->surface_[i];
					solid = solid && maps->biome_[i] < 3
						&& serial.get(x, h, z) != kAir && serial.get(x, std::max<int>(h, maps->waterTop_[i]) + 1, z) == kAir;

					// Under a mesa's soil lies its banded rock, and the recipe's base
					// only below the 3 m shell.
					if (maps->biome_[i] == 2)
					{
						const int rock = h - maps->topDepth_[i] - maps->soilDepth_[i];
						if (rock >= 0)
							deepBase = deepBase && (serial.get(x, rock, z) == redrock || serial.get(x, rock, z) == sandstone);
						if (rock - 31 >= 0)
							deepBase = deepBase && serial.get(x, rock - 31, z) == stone;
					}
					seen[std::min<int>(maps->biome_[i], 2)] = true;
				}
				check(solid, "the filled biome world disagrees with the terrain maps");
				check(deepBase, "a biome's banded rock is missing under its soil or reaches below its shell");
				check(seen[0] && seen[1] && seen[2], "a chosen biome covers no column of a small map");

				// Landform and biomes are alternatives, and biomes need version 3.
				const auto rejects = [&](const std::string& bad)
				{
					try { reparse(bad); }
					catch (const std::exception&) { return true; }
					return false;
				};
				std::string both = text;
				both.insert(both.find("\"biomes\""), "\"shape\": { \"baseHeight\": 2.0 }, ");
				check(rejects(both), "a recipe with both \"shape\" and \"biomes\" was accepted");

				std::string old = text;
				old.replace(old.find("\"version\": 3"), 12, "\"version\": 2");
				check(rejects(old), "a version 2 recipe with \"biomes\" was accepted");

				bool unknown = false;
				settings.biomes_ = { "flat", "lavascape" };
				try { composeBiomeRecipe(library, settings); }
				catch (const std::exception&) { unknown = true; }
				check(unknown, "composeBiomeRecipe accepted a biome the library does not have");
			}
			catch (const std::exception& e)
			{
				std::cerr << "[voxel-check] biome terrain threw: " << e.what() << "\n";
				++failures;
			}

			// A shaped (version 2) recipe round-trips too.
			try
			{
				const std::string json = R"({ "version": 2, "seed": 3, "size": { "x": 64, "y": 64, "z": 64 }, "origin": [1.5, 0, -2],
					"shape": { "ring": { "inner": 0.3, "outer": 0.9 }, "basins": [ { "centre": [0.25, 0.75], "radius": 3.0 } ] },
					"water": { "minArea": 2.0 }, "caves": { "enabled": false, "frequency": 0.02 } })";
				const Recipe recipe = Recipe::parse(std::span(reinterpret_cast<const unsigned char*>(json.data()), json.size()));
				const std::string text = recipe.toJson();
				check(Recipe::parse(std::span(reinterpret_cast<const unsigned char*>(text.data()), text.size())) == recipe,
					"a shaped recipe does not survive toJson() and parse()");
			}
			catch (const std::exception& e)
			{
				std::cerr << "[voxel-check] recipe round trip threw: " << e.what() << "\n";
				++failures;
			}
		}

		// ---------------------- Paged storage ------------------------
		//
		// A world keeps its sections in pages, one per cell and level. A
		// bounded world has them all and must read and write like one flat
		// grid; a streamed one has only what was installed, reads air where
		// it has nothing, and must refuse a write there rather than conjure
		// the page - an evicted cell would look resident again.
		try
		{
			// 20 x 3 x 9 sections: three cells by two, the far ones partial.
			const WorldDims dims = WorldDims::fromVoxels({ 20 * 32, 3 * 32, 9 * 32 }, 0.1f, glm::vec3{ 0.f });

			World eager(dims);
			check(eager.cells() == glm::ivec2{ 3, 2 } && eager.pageCount(0) == 6 && eager.pageCount(3) == 6,
				"a bounded world does not hold a page for every cell");
			check(eager.sectionCount(0) == 20u * 3u * 9u && eager.sectionsAt(3) == glm::ivec3{ 3, 1, 2 },
				"paging changed the section grid");

			// Every section gets its own index as a material; read back by
			// coordinate and by index, across page borders.
			bool flat = true;
			for (std::size_t i = 0; i < eager.sectionCount(0); ++i)
				eager.editSectionAt(0, i) = Section::uniform(static_cast<MaterialId>(1 + i % 200));
			for (std::size_t i = 0; i < eager.sectionCount(0) && flat; ++i)
			{
				const glm::ivec3 c = eager.sectionCoord(0, i);
				flat = eager.sectionIndex(0, c.x, c.y, c.z) == i
					&& eager.section(0, c.x, c.y, c.z).uniformMaterial() == static_cast<MaterialId>(1 + i % 200)
					&& eager.get(c.x * 32 + 5, c.y * 32 + 6, c.z * 32 + 7) == static_cast<MaterialId>(1 + i % 200);
			}
			check(flat, "a paged world does not read like a flat grid");

			World sparse(dims, World::Storage::Sparse);
			check(sparse.pageCount(0) == 0 && sparse.get(40, 40, 40) == kAir && sparse.section(0, 9, 1, 3).isAir()
				&& sparse.childMask(1, 2, 0, 1) == 0,
				"a streamed world with nothing installed does not read as air");
			check(!sparse.set(40, 40, 40, stone) && !sparse.hasDirty(), "a voxel of an absent cell was written");

			bool threw = false;
			try { sparse.editSection(0, 1, 1, 1) = Section::uniform(stone); }
			catch (const std::logic_error&) { threw = true; }
			check(threw && sparse.pageCount(0) == 0, "writing a section of an absent cell created its page");

			// Cell (1, 0) is sections x 8..15, z 0..7.
			std::vector<Section> page(sparse.pageSectionCount(0));
			page[World::pageSlot(0, 2, 1, 5)] = Section::uniform(grass);
			sparse.installPage(0, 1, 0, std::move(page), {});

			check(sparse.hasPage(0, 1, 0) && !sparse.hasPage(1, 1, 0) && !sparse.hasPage(0, 0, 0) && sparse.pageCount(0) == 1,
				"installPage touched more than its cell and level");
			check(sparse.section(0, 10, 1, 5).uniformMaterial() == grass && sparse.get(10 * 32, 40, 5 * 32) == grass
				&& sparse.get(9 * 32, 40, 5 * 32) == kAir,
				"an installed page does not read back where pageSlot() put it");
			check(sparse.sectionResident(0, 8, 0, 0) && !sparse.sectionResident(0, 7, 0, 0) && !sparse.sectionResident(0, 8, 0, 8),
				"sectionResident disagrees with the cell borders");
			check(sparse.voxelsResident({ 8 * 32, 0, 0 }, { 16 * 32, 96, 8 * 32 }) && !sparse.voxelsResident({ 8 * 32 - 1, 0, 0 }, { 9 * 32, 8, 8 }),
				"voxelsResident does not follow the cells a box touches");

			check(sparse.set(10 * 32 + 1, 40, 5 * 32 + 1, stone) && sparse.hasDirty(), "a voxel of a resident cell could not be written");

			bool wrongSize = false;
			try { sparse.installPage(1, 1, 0, std::vector<Section>(3), {}); }
			catch (const std::invalid_argument&) { wrongSize = true; }
			check(wrongSize, "installPage took a page of the wrong size");

			sparse.removePage(0, 1, 0);
			check(sparse.pageCount(0) == 0 && sparse.get(10 * 32, 40, 5 * 32) == kAir && !sparse.hasDirty(),
				"a removed page still reads, or left a dirty mark behind");
		}
		catch (const std::exception& e)
		{
			std::cerr << "[voxel-check] paged storage threw: " << e.what() << "\n";
			++failures;
		}

		// ------------------------ Splines ---------------------------
		{
			using util::noise::Spline;
			const Spline s({ { -1.f, 10.f }, { 0.f, 20.f }, { 1.f, 60.f } });
			check(nearly(s.eval(-5.f), 10.f, 1e-6) && nearly(s.eval(5.f), 60.f, 1e-6), "spline does not clamp at its ends");
			check(nearly(s.eval(-0.5f), 15.f, 1e-5) && nearly(s.eval(0.5f), 40.f, 1e-5), "spline does not interpolate linearly");
			check(nearly(Spline{}.eval(0.3f), 0.f, 1e-6), "empty spline is not zero");
		}

		std::cout << (failures == 0
			? "[voxel-check] all checks passed\n"
			: "[voxel-check] SOME CHECKS FAILED\n");
	}
}
