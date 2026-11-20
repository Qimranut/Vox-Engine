/*************************************************************************************************************
\file       voxel_renderer.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Implements VoxelRenderer: GL setup, the growable quad arena, budgeted mesh uploads, the
            frontier-only LoD traversal with per-bucket back-face culling, and the indirect draw.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "voxel_renderer.hpp"

// Voxel
#include "../sys/voxel_sys.hpp"
#include "../world/voxel_math.hpp"
#include "lod_traversal.hpp"

// Graphics
#include "../../graphics/camera/camera.hpp"
#include "../../graphics/renderer/line_renderer.hpp"
#include "../../graphics/shaders/shader.hpp"

// Profiling
#include "../../profile/profiler_gpu.hpp"

// stds
#include <algorithm>
#include <iostream>
#include <stdexcept>


namespace voxel::render
{
	namespace
	{
		// A bucket never exceeds 16384 quads (a checkerboard), so one shared
		// index buffer of that many quads serves every draw via baseVertex.
		constexpr std::uint32_t kIndexQuads = 16384;

		constexpr GLuint kQuadBinding = 0;
		constexpr GLuint kMaterialBinding = 1;
	}

	VoxelRenderer::VoxelRenderer() = default;

	/*!
		@brief
			Calls shutdown().
		*/
	VoxelRenderer::~VoxelRenderer()
	{
		shutdown();
	}

	/*!
		@brief
			Builds the shader, arena, shared index buffer, material buffer, VAO and line renderer.

			Needs a current 4.5 context; does nothing when already initialised.
			The arena may later grow up to the SSBO block limit, capped at 2 GiB.
		@param settings
			Live settings; read every frame, must outlive the renderer
		*/
	void VoxelRenderer::init(const RenderSettings& settings)
	{
		if (initialised_)
			return;

		settings_ = &settings;

		program_ = LoadShaders(
			"Engine/Source/graphics/shaders/VoxelQuad.vert",
			"Engine/Source/graphics/shaders/VoxelQuad.frag");
		if (program_ == 0)
			throw std::runtime_error("[voxel] VoxelQuad shader failed to build");

		locViewProj_  = glGetUniformLocation(program_, "u_viewProj");
		locOrigin_    = glGetUniformLocation(program_, "u_origin");
		locVoxelSize_ = glGetUniformLocation(program_, "u_voxelSize");
		locFaceTint_  = glGetUniformLocation(program_, "u_faceTint");
		locFlags_     = glGetUniformLocation(program_, "u_flags");

		// Geometry arena: immutable storage, filled with SubData under a budget.
		const std::size_t arenaBytes = std::max<std::size_t>(settings.arenaBytes_, 1u << 20);
		arenaBuffer_.storage(arenaBytes, nullptr, GL_DYNAMIC_STORAGE_BIT);
		arena_.reset(static_cast<std::uint32_t>(arenaBytes / sizeof(Quad)));

		// The arena grows on demand up to what one SSBO binding may address
		// (the shader sees it as one block), and never past 2 GiB, which keeps
		// baseVertex (a signed int, four vertices per quad) well clear of overflow.
		{
			GLint64 maxBlock = 0;
			glGetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &maxBlock);
			const std::uint64_t limit = std::min<std::uint64_t>(maxBlock > 0 ? static_cast<std::uint64_t>(maxBlock) : arenaBytes,
				std::uint64_t{ 2 } << 30);
			maxArenaQuads_ = std::max<std::uint64_t>(limit / sizeof(Quad), arena_.capacity());
		}

		// Shared index pattern: (0,1,2)(2,1,3) per quad.
		{
			std::vector<std::uint32_t> indices;
			indices.reserve(static_cast<std::size_t>(kIndexQuads) * 6);
			for (std::uint32_t q = 0; q < kIndexQuads; ++q)
			{
				const std::uint32_t b = q * 4;
				indices.insert(indices.end(), { b + 0, b + 1, b + 2, b + 2, b + 1, b + 3 });
			}
			indexBuffer_.storage(indices.size() * sizeof(std::uint32_t), indices.data(), 0);
		}

		materialBuffer_.storage(256 * sizeof(glm::vec4), nullptr, GL_DYNAMIC_STORAGE_BIT);
		if (materialColours_.empty())
			materialColours_.assign(256, glm::vec4{ 1.f, 0.f, 1.f, 1.f });
		materialsDirty_ = true;

		// One instanced uvec4 attribute: the section record for the draw.
		glCreateVertexArrays(1, &vao_);
		glEnableVertexArrayAttrib(vao_, 0);
		glVertexArrayAttribIFormat(vao_, 0, 4, GL_UNSIGNED_INT, 0);
		glVertexArrayAttribBinding(vao_, 0, 0);
		glVertexArrayBindingDivisor(vao_, 0, 1);
		glVertexArrayElementBuffer(vao_, indexBuffer_.id());

		lineRenderer_ = std::make_unique<graphics::renderer::LineRenderer>(LoadShaders(
			"Engine/Source/graphics/shaders/DebugLine.vert",
			"Engine/Source/graphics/shaders/DebugLine.frag"));

		initialised_ = true;
	}

	/*!
		@brief
			Deletes every GL object and forgets the world, nodes and queued uploads.
		*/
	void VoxelRenderer::shutdown()
	{
		if (!initialised_)
			return;

		lineRenderer_.reset();
		if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
		if (program_) { glDeleteProgram(program_); program_ = 0; }
		arenaBuffer_.release();
		materialBuffer_.release();
		indexBuffer_.release();
		recordBuffer_.release();
		indirectBuffer_.release();

		uploads_.clear();
		for (auto& level : nodePages_)
			level.clear();
		rootCells_.clear();
		residentNodes_ = residentQuads_ = 0;
		world_.reset();
		initialised_ = false;
	}

	/*!
		@brief
			Copies the materials' linear colours (magenta for unused ids) for upload on the next render.
		@param materials
			Material table
		*/
	void VoxelRenderer::setMaterials(const MaterialTable& materials)
	{
		materialColours_.assign(256, glm::vec4{ 1.f, 0.f, 1.f, 1.f });
		for (std::size_t i = 0; i < materials.size() && i < 256; ++i)
			materialColours_[i] = glm::vec4(materials.get(static_cast<MaterialId>(i)).colourLinear_, 1.f);
		materialsDirty_ = true;
	}

	/*!
		@brief
			Releases every node of the previous world, resizes the arena if needed and installs the new one.
		@param world
			World to draw, or nullptr to clear
		@param arenaBytes
			Quad arena size for this world; 0 for the settings' size
		*/
	void VoxelRenderer::setWorld(std::shared_ptr<const World> world, std::size_t arenaBytes)
	{
		uploads_.clear();
		for (auto& level : nodePages_)
		{
			for (std::unique_ptr<NodePage>& page : level)
			{
				if (!page)
					continue;
				for (Node& n : page->nodes_)
					releaseNode(n);
			}
			level.clear();
		}
		rootCells_.clear();

		world_ = std::move(world);

		// Everything was just released, so the arena is empty and may change size.
		const std::size_t wanted = std::max<std::size_t>(arenaBytes != 0 ? arenaBytes : (settings_ ? settings_->arenaBytes_ : (64u << 20)), 1u << 20);
		if (wanted / sizeof(Quad) != arena_.capacity())
		{
			arena_.reset(static_cast<std::uint32_t>(wanted / sizeof(Quad)));
			arenaResizePending_ = true;
			arenaFullLogged_ = false;
		}

		if (!world_)
			return;

		const glm::ivec3 sections = world_->dims().sections_;
		if (sections.x > kMaxSectionsXZ || sections.z > kMaxSectionsXZ || sections.y > kMaxSectionsY)
			std::cerr << "[voxel] the world is larger than a section record can address (" << kMaxSectionsXZ << " x "
				<< kMaxSectionsY << " x " << kMaxSectionsXZ << " sections): far sections will draw in the wrong place\n";

		const glm::ivec2 cells = world_->cells();
		for (auto& level : nodePages_)
			level.resize(static_cast<std::size_t>(cells.x) * static_cast<std::size_t>(cells.y));
	}

	/*!
		@brief
			Looks up a node without creating its page.
		@param level
			LoD level
		@param coord
			Section coordinate at that level
		@return
			The node, or nullptr when its page does not exist
		*/
	const VoxelRenderer::Node* VoxelRenderer::nodeOrNull(int level, const glm::ivec3& coord) const
	{
		const glm::ivec2 cell = World::cellOf(level, coord.x, coord.z);
		const NodePage* page = nodePages_[level][static_cast<std::size_t>(cell.y) * world_->cells().x + cell.x].get();
		if (!page)
			return nullptr;

		const int mask = World::pageSide(level) - 1;
		return &page->nodes_[World::pageSlot(level, coord.x & mask, coord.y, coord.z & mask)];
	}

	/*!
		@brief
			Non-const lookup of a node, through the const overload.
		@param level
			LoD level
		@param coord
			Section coordinate at that level
		@return
			The node, or nullptr when its page does not exist
		*/
	VoxelRenderer::Node* VoxelRenderer::nodeOrNull(int level, const glm::ivec3& coord)
	{
		return const_cast<Node*>(static_cast<const VoxelRenderer&>(*this).nodeOrNull(level, coord));
	}

	/*!
		@brief
			Looks up a node, creating its page (and a root cell at the top level) if needed.
		@param level
			LoD level
		@param coord
			Section coordinate at that level
		@return
			The node
		*/
	VoxelRenderer::Node& VoxelRenderer::nodeFor(int level, const glm::ivec3& coord)
	{
		const glm::ivec2 cell = World::cellOf(level, coord.x, coord.z);
		std::unique_ptr<NodePage>& page = nodePages_[level][static_cast<std::size_t>(cell.y) * world_->cells().x + cell.x];

		if (!page)
		{
			page = std::make_unique<NodePage>();
			page->nodes_.resize(world_->pageSectionCount(level));

			if (level == kLodLevels - 1)
				rootCells_.push_back(cell);
		}

		const int mask = World::pageSide(level) - 1;
		return page->nodes_[World::pageSlot(level, coord.x & mask, coord.y, coord.z & mask)];
	}

	/*!
		@brief
			Frees a node's arena slots, updates the resident counters and resets it to never meshed.
		@param node
			Node to release
		*/
	void VoxelRenderer::releaseNode(Node& node)
	{
		if (node.state_ == NodeState::Resident)
		{
			--residentNodes_;
			residentQuads_ -= node.quadCount_;
		}

		if (node.arenaOffset_ != QuadArena::kNone)
			arena_.free(node.arenaOffset_, node.quadCount_);
		node = Node{};
	}

	/*!
		@brief
			Drops a cell's page at one level with its queued uploads, leaving its nodes never meshed.

			Never submit an empty mesh to evict: an empty mesh counts as meshed
			and the traversal would draw a hole.
		@param level
			LoD level
		@param cx
			Cell x
		@param cz
			Cell z
		*/
	void VoxelRenderer::evictCell(int level, int cx, int cz)
	{
		if (!world_ || level < 0 || level >= kLodLevels || !world_->validCell(cx, cz))
			return;

		std::unique_ptr<NodePage>& page = nodePages_[level][static_cast<std::size_t>(cz) * world_->cells().x + cx];
		if (!page)
			return;

		std::erase_if(uploads_, [&](const Upload& u)
		{
			return u.level_ == level && World::cellOf(level, u.coord_.x, u.coord_.z) == glm::ivec2{ cx, cz };
		});

		for (Node& n : page->nodes_)
			releaseNode(n);
		page.reset();

		if (level == kLodLevels - 1)
			std::erase(rootCells_, glm::ivec2{ cx, cz });
	}

	/*!
		@brief
			Replaces a node's mesh: allocates arena slots (growing the arena if full) and queues the upload.

			An empty mesh just marks the node meshed. When the arena cannot
			grow the node is left not meshed, so its parent goes on being drawn.
		@param level
			LoD level
		@param coord
			Section coordinate at that level
		@param mesh
			The new mesh; its quads are moved into the upload queue
		*/
	void VoxelRenderer::submitMesh(int level, const glm::ivec3& coord, SectionMesh&& mesh)
	{
		if (!world_ || !world_->validSection(level, coord.x, coord.y, coord.z))
			return;

		Node& node = nodeFor(level, coord);

		// Drop any upload still queued for this node.
		if (node.state_ == NodeState::Pending)
			std::erase_if(uploads_, [&](const Upload& u) { return u.level_ == level && u.coord_ == coord; });

		releaseNode(node);

		if (mesh.empty())
		{
			node.meshed_ = true;
			return;
		}

		std::uint32_t offset = arena_.allocate(mesh.quadCount());
		if (offset == QuadArena::kNone && growArena(mesh.quadCount()))
			offset = arena_.allocate(mesh.quadCount());

		if (offset == QuadArena::kNone)
		{
			// Left "not meshed", never "meshed and empty": the traversal would
			// descend into an empty node and draw a hole, where this way the
			// parent goes on being drawn.
			if (!arenaFullLogged_)
			{
				std::cerr << "[voxel] quad arena is full at " << ((static_cast<std::size_t>(arena_.capacity()) * sizeof(Quad)) >> 20)
					<< " MiB and cannot grow - some sections draw at a coarser level\n";
				arenaFullLogged_ = true;
			}
			return;
		}

		node.meshed_ = true;
		node.arenaOffset_ = offset;
		node.quadCount_ = mesh.quadCount();
		std::copy(std::begin(mesh.counts_), std::end(mesh.counts_), std::begin(node.counts_));
		node.aabb_ = mesh.aabb_;
		node.state_ = NodeState::Pending;

		uploads_.push_back(Upload{ level, coord, offset, std::move(mesh.quads_) });
	}

	/*!
		@brief
			Grows the arena's CPU capacity by doubling (at least by quads), up to its limit.
		@param quads
			Number of extra quads needed
		@return
			False when the arena is at its limit
		*/
	bool VoxelRenderer::growArena(std::uint32_t quads)
	{
		const std::uint64_t capacity = arena_.capacity();
		if (capacity >= maxArenaQuads_)
			return false;

		// Doubling keeps the number of copies logarithmic in the final size.
		const std::uint64_t wanted = std::min<std::uint64_t>(std::max<std::uint64_t>(capacity * 2, capacity + quads), maxArenaQuads_);
		if (wanted < capacity + quads)
			return false;

		arena_.grow(static_cast<std::uint32_t>(wanted));
		std::cerr << "[voxel] quad arena grown to " << ((wanted * sizeof(Quad)) >> 20) << " MiB\n";
		return true;
	}

	/*!
		@brief
			Uploads queued meshes into the arena buffer under the byte budget and marks them resident.
		*/
	void VoxelRenderer::drainUploads()
	{
		VOX_PROFILE_SCOPE_C("Upload quads", profile::colour::kVoxel);

		std::size_t budget = settings_ ? settings_->uploadBudgetBytes_ : (8u << 20);

		while (!uploads_.empty() && budget > 0)
		{
			Upload& u = uploads_.front();
			const std::size_t bytes = u.quads_.size() * sizeof(Quad);

			arenaBuffer_.subData(static_cast<std::size_t>(u.offset_) * sizeof(Quad), bytes, u.quads_.data());

			// Null only if the cell went while this waited, and evictCell
			// takes its uploads with it; checked all the same.
			if (Node* node = nodeOrNull(u.level_, u.coord_); node && node->state_ == NodeState::Pending)
			{
				node->state_ = NodeState::Resident;
				++residentNodes_;
				residentQuads_ += node->quadCount_;
			}

			budget -= std::min(budget, bytes);
			uploads_.pop_front();
		}
	}

	/*!
		@brief
			Tests whether every existing child of a node is meshed and uploaded.
		@param level
			LoD level of the parent
		@param coord
			Parent section coordinate
		@return
			true when the node may be replaced by its children
		*/
	bool VoxelRenderer::childrenResident(int level, const glm::ivec3& coord) const
	{
		const std::uint8_t mask = world_->childMask(level, coord.x, coord.y, coord.z);
		for (int k = 0; k < 8; ++k)
		{
			if (!(mask & (1u << k)))
				continue;

			const glm::ivec3 c{ 2 * coord.x + ((k >> 2) & 1), 2 * coord.y + ((k >> 1) & 1), 2 * coord.z + (k & 1) };
			if (!world_->validSection(level - 1, c.x, c.y, c.z))
				continue;

			// Waiting on its first mesh, or on the upload of one - or, in a
			// streamed world, not there at all: the cell holds this level and
			// not the one below.
			const Node* n = nodeOrNull(level - 1, c);
			if (!n || !n->meshed_ || n->state_ == NodeState::Pending)
				return false;
		}
		return true;
	}

	/*!
		@brief
			Frustum-culls a node, then descends into its children or emits it as part of the LoD frontier.

			Descends only when the children are all resident and the camera is
			inside the node or its projected area passes the threshold.
		@param level
			LoD level
		@param coord
			Section coordinate at that level
		@param camera
			Camera to draw from
		@param threshold
			Projected-area split threshold
		*/
	void VoxelRenderer::visit(int level, const glm::ivec3& coord, const graphics::camera::Camera& camera, float threshold)
	{
		++stats_.nodesVisited_;

		glm::vec3 mn, mx;
		sectionBounds(world_->dims(), level, coord, mn, mx);

		if (!camera.intersects(mn, mx))
			return;

		const std::uint8_t mask = world_->childMask(level, coord.x, coord.y, coord.z);

		bool descend = false;
		if (level > 0 && mask != 0 && childrenResident(level, coord))
		{
			descend = pointInside(camera.position(), mn, mx)
				|| projectedArea(camera.viewProjection(), mn, mx) > threshold;
		}

		if (descend)
		{
			for (int k = 0; k < 8; ++k)
			{
				if (!(mask & (1u << k)))
					continue;
				const glm::ivec3 c{ 2 * coord.x + ((k >> 2) & 1), 2 * coord.y + ((k >> 1) & 1), 2 * coord.z + (k & 1) };
				if (world_->validSection(level - 1, c.x, c.y, c.z))
					visit(level - 1, c, camera, threshold);
			}
			return;
		}

		const Node* node = nodeOrNull(level, coord);
		if (node && node->state_ == NodeState::Resident && node->quadCount_ > 0)
			emit(level, coord, *node, camera.position(), mn, mx);
	}

	/*!
		@brief
			Adds a node's section record and one indirect draw per camera-facing bucket.
		@param level
			LoD level
		@param coord
			Section coordinate at that level
		@param node
			The resident node
		@param cameraPos
			Camera position, for bucket back-face culling
		@param mn
			Section bounds minimum
		@param mx
			Section bounds maximum
		*/
	void VoxelRenderer::emit(int level, const glm::ivec3& coord, const Node& node, const glm::vec3& cameraPos,
							 const glm::vec3& mn, const glm::vec3& mx)
	{
		const std::uint32_t recordIndex = static_cast<std::uint32_t>(records_.size());

		SectionRecord rec{};
		rec.posLod_ = packPosLod(coord.x, coord.y, coord.z, level);
		rec.aabb_ = node.aabb_;
		rec.geometryOffset_ = node.arenaOffset_;
		std::copy(std::begin(node.counts_), std::end(node.counts_), std::begin(rec.counts_));
		records_.push_back(rec);

		std::uint32_t prefix = 0;
		for (int f = 0; f < FaceCount; ++f)
		{
			const std::uint32_t count = node.counts_[f];
			const std::uint32_t bucketOffset = prefix;
			prefix += count;

			if (count == 0)
				continue;

			// Section-level back-face culling: a +X bucket can only be seen
			// from beyond the section's minimum x, and so on. The quad axis
			// code is the quad format's order (0 = Y, 1 = Z, 2 = X), not a vec3 index.
			static constexpr int kComponent[3] = { 1, 2, 0 };
			const int c = kComponent[faceAxis(f)];
			const bool visible = faceSide(f) ? cameraPos[c] > mn[c] : cameraPos[c] < mx[c];
			if (!visible)
				continue;

			DrawCommand cmd{};
			cmd.count_ = count * 6;
			cmd.instanceCount_ = 1;
			cmd.firstIndex_ = 0;
			cmd.baseVertex_ = static_cast<std::int32_t>((node.arenaOffset_ + bucketOffset) * 4);
			cmd.baseInstance_ = recordIndex;
			commands_.push_back(cmd);

			stats_.quads_ += count;
		}

		if (settings_ && settings_->sectionBounds_)
			pushBounds(mn, mx, level);
	}

	/*!
		@brief
			Adds a box's twelve edges, coloured by level, to the section bounds lines.
		@param mn
			Box minimum
		@param mx
			Box maximum
		@param level
			LoD level, picks the colour
		*/
	void VoxelRenderer::pushBounds(const glm::vec3& mn, const glm::vec3& mx, int level)
	{
		static const glm::vec3 kColours[4] = { { 0.9f, 0.9f, 0.9f }, { 0.2f, 0.9f, 0.2f }, { 0.2f, 0.4f, 1.f }, { 1.f, 0.3f, 0.3f } };
		const glm::vec3 c = kColours[level & 3];

		const glm::vec3 p[8] = {
			{ mn.x, mn.y, mn.z }, { mx.x, mn.y, mn.z }, { mx.x, mn.y, mx.z }, { mn.x, mn.y, mx.z },
			{ mn.x, mx.y, mn.z }, { mx.x, mx.y, mn.z }, { mx.x, mx.y, mx.z }, { mn.x, mx.y, mx.z } };
		static const int e[12][2] = { {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7} };

		for (const auto& edge : e)
		{
			boundsLines_.push_back({ p[edge[0]], c });
			boundsLines_.push_back({ p[edge[1]], c });
		}
	}

	/*!
		@brief
			Visits every section column of the root cells at the top level.
		@param camera
			Camera to draw from
		@param threshold
			Projected-area split threshold
		*/
	void VoxelRenderer::traverse(const graphics::camera::Camera& camera, float threshold)
	{
		VOX_PROFILE_SCOPE_C("LoD traversal", profile::colour::kVoxel);

		// From the cells that have anything meshed, not from the whole grid:
		// a streamed world's grid is kilometres of cells that are not there.
		// At the top level a cell is one section column.
		const int top = kLodLevels - 1;
		const int height = world_->sectionsAt(top).y;

		for (const glm::ivec2& cell : rootCells_)
			for (int sy = 0; sy < height; ++sy)
				visit(top, { cell.x, sy, cell.y }, camera, threshold);
	}

	/*!
		@brief
			Applies pending material and arena buffer changes, uploads meshes, traverses and draws the terrain.
		@param camera
			Camera to draw from
		@param viewportWidth
			Viewport width in pixels
		@param viewportHeight
			Viewport height in pixels
		*/
	void VoxelRenderer::render(const graphics::camera::Camera& camera, int viewportWidth, int viewportHeight)
	{
		if (!initialised_)
			return;

		VOX_PROFILE_SCOPE_C("Voxel render", profile::colour::kVoxel);

		stats_ = FrameStats{};
		stats_.arenaBytes_ = arenaBuffer_.size();

		if (materialsDirty_)
		{
			materialBuffer_.subData(0, materialColours_.size() * sizeof(glm::vec4), materialColours_.data());
			materialsDirty_ = false;
		}

		if (arenaResizePending_)
		{
			arenaBuffer_.release();
			arenaBuffer_.storage(static_cast<std::size_t>(arena_.capacity()) * sizeof(Quad), nullptr, GL_DYNAMIC_STORAGE_BIT);
			arenaResizePending_ = false;
			stats_.arenaBytes_ = arenaBuffer_.size();
		}
		else if (static_cast<std::size_t>(arena_.capacity()) * sizeof(Quad) > arenaBuffer_.size())
		{
			// Grown by submitMesh: same offsets, so the old quads are copied
			// across before anything new is uploaded.
			GlBuffer grown;
			grown.storage(static_cast<std::size_t>(arena_.capacity()) * sizeof(Quad), nullptr, GL_DYNAMIC_STORAGE_BIT);
			glCopyNamedBufferSubData(arenaBuffer_.id(), grown.id(), 0, 0, static_cast<GLsizeiptr>(arenaBuffer_.size()));
			arenaBuffer_ = std::move(grown);
			stats_.arenaBytes_ = arenaBuffer_.size();
		}

		if (!world_)
			return;

		drainUploads();

		stats_.uploadsPending_ = static_cast<std::uint32_t>(uploads_.size());
		stats_.arenaUsedBytes_ = static_cast<std::size_t>(arena_.used()) * sizeof(Quad);
		stats_.residentNodes_ = residentNodes_;
		stats_.residentQuads_ = residentQuads_;

		records_.clear();
		commands_.clear();
		boundsLines_.clear();

		const float subdiv = settings_ ? settings_->subDivisionSize_ : 64.f;
		traverse(camera, descendThreshold(subdiv, viewportWidth, viewportHeight));

		stats_.draws_ = static_cast<std::uint32_t>(commands_.size());

		if (!commands_.empty())
		{
			VOX_PROFILE_SCOPE("Submit terrain draws");
			VOX_PROFILE_VALUE(commands_.size());
			VOX_PROFILE_GPU_SCOPE_C("Terrain", profile::colour::kVoxel);

			recordBuffer_.data(records_.size() * sizeof(SectionRecord), records_.data(), GL_STREAM_DRAW);
			const bool indirect = !settings_ || settings_->multiDrawIndirect_;
			if (indirect)
				indirectBuffer_.data(commands_.size() * sizeof(DrawCommand), commands_.data(), GL_STREAM_DRAW);

			glVertexArrayVertexBuffer(vao_, 0, recordBuffer_.id(), 0, sizeof(SectionRecord));

			glUseProgram(program_);
			glBindVertexArray(vao_);
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kQuadBinding, arenaBuffer_.id());
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kMaterialBinding, materialBuffer_.id());
			glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirect ? indirectBuffer_.id() : 0);

			const WorldDims& d = world_->dims();
			glUniformMatrix4fv(locViewProj_, 1, GL_FALSE, &camera.viewProjection()[0][0]);
			glUniform3fv(locOrigin_, 1, &d.origin_[0]);
			glUniform1f(locVoxelSize_, d.voxelSize_);
			const glm::vec4 tint = settings_ ? settings_->faceTint_ : glm::vec4{ 1.f, 0.5f, 0.8f, 0.6f };
			glUniform4fv(locFaceTint_, 1, &tint[0]);

			GLuint flags = 0;
			if (settings_ && settings_->ambientOcclusion_) flags |= 1u;
			if (settings_ && settings_->lodColours_)       flags |= 2u;
			glUniform1ui(locFlags_, flags);

			const bool wire = settings_ && settings_->wireframe_;
			if (wire)
				glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

			if (indirect)
				glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, nullptr,
					static_cast<GLsizei>(commands_.size()), 0);
			else
			{
				// Submit the same commands individually on drivers with MDI issues.
				// Preserve baseInstance: it selects each draw's section record.
				for (const DrawCommand& cmd : commands_)
					glDrawElementsInstancedBaseVertexBaseInstance(GL_TRIANGLES,
						static_cast<GLsizei>(cmd.count_), GL_UNSIGNED_INT,
						reinterpret_cast<const void*>(static_cast<std::uintptr_t>(cmd.firstIndex_) * sizeof(std::uint32_t)),
						static_cast<GLsizei>(cmd.instanceCount_), cmd.baseVertex_, cmd.baseInstance_);
			}

			if (wire)
				glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

			glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kMaterialBinding, 0);
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kQuadBinding, 0);
			glBindVertexArray(0);
			glUseProgram(0);
		}

		if (!boundsLines_.empty() && lineRenderer_)
			lineRenderer_->draw(boundsLines_, camera.viewProjection());
	}
}
