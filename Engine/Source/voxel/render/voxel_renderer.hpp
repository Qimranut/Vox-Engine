/*************************************************************************************************************
\file       voxel_renderer.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      VoxelRenderer: draws the voxel world from one quad arena SSBO with vertex pulling, a CPU LoD
            traversal and a single multi-draw-indirect call, with paged nodes for streamed worlds.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Voxel
#include "../types/voxel_types.hpp"
#include "../types/material_table.hpp"
#include "../mesh/section_mesh.hpp"
#include "../world/world.hpp"
#include "gl_buffer.hpp"
#include "quad_arena.hpp"
#include "draw_types.hpp"

// Graphics
#include "../../graphics/renderer/world_pass.hpp"
#include "../../graphics/renderer/debug_vertex.hpp"

// GLEW
#include <GL/glew.h>

// GLM
#include <glm/glm.hpp>

// stds
#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>


namespace graphics::renderer { class LineRenderer; }

namespace voxel
{
	struct RenderSettings;
}

namespace voxel::render
{
	struct FrameStats
	{
		std::uint32_t	draws_			{ 0 };
		std::uint32_t	quads_			{ 0 };
		std::uint32_t	nodesVisited_	{ 0 };
		std::uint32_t	uploadsPending_	{ 0 };
		std::uint32_t	residentNodes_	{ 0 };
		std::uint32_t	residentQuads_	{ 0 };
		std::size_t		arenaUsedBytes_	{ 0 };
		std::size_t		arenaBytes_		{ 0 };
	};

	/**
	 * Draws a World with four pieces: 8-byte quads in one arena SSBO,
	 * vertex pulling through a shared index buffer, one
	 * glMultiDrawElementsIndirect, and a per-frame CPU LoD traversal that
	 * renders each node iff it is the frontier - never a parent and its
	 * children together.
	 *
	 * Main-thread object. submitMesh()/setWorld() are GL-free and may run
	 * in voxel::System::update(); every GL call lives in init(),
	 * shutdown() and render().
	 *
	 */
	class VoxelRenderer final : public graphics::renderer::WorldPass
	{
	public:

		/*!
			@brief
				Creates an uninitialised renderer; init() creates the GL objects.
			*/
		VoxelRenderer();

		/*!
			@brief
				Calls shutdown().
			*/
		~VoxelRenderer() override;

		/*!
			@brief
				Creates every GL object. Needs a current 4.5 context.
			@param settings
				Live settings; read every frame, must outlive the renderer
			*/
		void init(const RenderSettings& settings);

		/*!
			@brief
				Deletes every GL object and forgets the world and its nodes.
			*/
		void shutdown();

		/*!
			@brief
				Uploads the material colours (next render).
			@param materials
				Material table whose linear colours are used
			*/
		void setMaterials(const MaterialTable& materials);

		/*!
			@brief
				Installs the world whose nodes are drawn.

				Releases every mesh of the previous one. nullptr clears.
			@param world
				World to draw, or nullptr
			@param arenaBytes
				Size of the quad arena for this world; 0 for the settings' size. A
				streamed world keeps several times a bounded map's quads resident.
				GL-free like the rest: the buffer itself is replaced by the next
				render(), before anything is uploaded into it
			*/
		void setWorld(std::shared_ptr<const World> world, std::size_t arenaBytes = 0);

		/*!
			@brief
				Replaces a node's mesh.

				Empty meshes just release the slot. A full arena grows (up to
				the SSBO block limit) rather than drop the mesh.
			@param level
				LoD level
			@param coord
				Section coordinate at that level
			@param mesh
				The new mesh; its quads are moved into the upload queue
			*/
		void submitMesh(int level, const glm::ivec3& coord, SectionMesh&& mesh);

		/*!
			@brief
				Forgets a cell's meshes at one level.

				Frees their quads, drops the uploads still queued for them and
				marks the nodes as never meshed. That last part is the point. A
				node that received an EMPTY mesh counts as meshed ("nothing to
				draw here"), and the traversal would descend into it and draw a
				hole; a node that was never meshed makes childrenResident() fail,
				so the parent goes on being drawn. This is how a streamed cell
				drops its fine levels. Never submit an empty mesh to evict.
			@param level
				LoD level
			@param cx
				Cell x
			@param cz
				Cell z
			*/
		void evictCell(int level, int cx, int cz);

		/*!
			@brief
				No uploads waiting.
			@return
				true when the upload queue is empty
			*/
		bool allResident() const { return uploads_.empty(); }

		/*!
			@brief
				Uploads pending quads under budget, runs the LoD traversal and draws the terrain.
			@param camera
				Camera to draw from
			@param viewportWidth
				Viewport width in pixels
			@param viewportHeight
				Viewport height in pixels
			*/
		void render(const graphics::camera::Camera& camera, int viewportWidth, int viewportHeight) override;

		/*!
			@brief
				Statistics of the last render().
			@return
				The frame stats
			*/
		const FrameStats& frameStats() const { return stats_; }

	private:

		enum class NodeState : std::uint8_t { Empty, Pending, Resident };

		struct Node
		{
			std::uint32_t	arenaOffset_	{ QuadArena::kNone };
			std::uint32_t	quadCount_		{ 0 };
			std::uint16_t	counts_[FaceCount]{};
			std::uint32_t	aabb_			{ 0 };
			NodeState		state_			{ NodeState::Empty };

			// A mesh result has arrived, even an empty one. A buried solid
			// section has content but no faces, so Empty alone cannot tell
			// "nothing to draw" from "still in flight".
			bool			meshed_			{ false };
		};

		struct Upload
		{
			int					level_;
			glm::ivec3			coord_;
			std::uint32_t		offset_;
			std::vector<Quad>	quads_;
		};

		// Nodes are paged like the World's sections (one page per cell and
		// level, laid out by World::pageSlot), and a page exists only once one
		// of its nodes has received a mesh: a streamed world is thousands of
		// cells wide and almost all of them are empty at any moment.
		struct NodePage
		{
			std::vector<Node>	nodes_;
		};

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
		Node* nodeOrNull(int level, const glm::ivec3& coord);

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
		const Node* nodeOrNull(int level, const glm::ivec3& coord) const;

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
		Node& nodeFor(int level, const glm::ivec3& coord);

		/*!
			@brief
				Frees a node's arena slots, updates the resident counters and resets it to never meshed.
			@param node
				Node to release
			*/
		void releaseNode(Node& node);

		/*!
			@brief
				Makes room for at least quads more slots.

				CPU side; render() replaces the buffer and copies the old quads
				across.
			@param quads
				Number of extra quads needed
			@return
				False when the arena is at its limit
			*/
		bool growArena(std::uint32_t quads);

		/*!
			@brief
				Uploads queued meshes into the arena buffer under the byte budget and marks them resident.
			*/
		void drainUploads();

		/*!
			@brief
				Visits every section column of the root cells at the top level.
			@param camera
				Camera to draw from
			@param threshold
				Projected-area split threshold
			*/
		void traverse(const graphics::camera::Camera& camera, float threshold);

		/*!
			@brief
				Culls a node, then descends into its children or emits it as part of the LoD frontier.
			@param level
				LoD level
			@param coord
				Section coordinate at that level
			@param camera
				Camera to draw from
			@param threshold
				Projected-area split threshold
			*/
		void visit(int level, const glm::ivec3& coord, const graphics::camera::Camera& camera, float threshold);

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
		bool childrenResident(int level, const glm::ivec3& coord) const;

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
		void emit(int level, const glm::ivec3& coord, const Node& node, const glm::vec3& cameraPos, const glm::vec3& mn, const glm::vec3& mx);

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
		void pushBounds(const glm::vec3& mn, const glm::vec3& mx, int level);

		const RenderSettings*	settings_	{ nullptr };
		std::shared_ptr<const World>	world_;
		std::array<std::vector<std::unique_ptr<NodePage>>, kLodLevels>	nodePages_;

		// Cells that have a page at the top level: where the traversal starts.
		std::vector<glm::ivec2>	rootCells_;

		// Kept as nodes change state, not counted every frame.
		std::uint32_t	residentNodes_	{ 0 };
		std::uint32_t	residentQuads_	{ 0 };

		QuadArena			arena_;
		std::deque<Upload>	uploads_;

		// How far growArena() may take the arena, from the SSBO block limit.
		std::uint64_t		maxArenaQuads_	{ 0 };

		GlBuffer	arenaBuffer_;
		GlBuffer	materialBuffer_;
		GlBuffer	indexBuffer_;
		GlBuffer	recordBuffer_;
		GlBuffer	indirectBuffer_;
		GLuint		vao_		{ 0 };
		GLuint		program_	{ 0 };
		GLint		locViewProj_	{ -1 };
		GLint		locOrigin_		{ -1 };
		GLint		locVoxelSize_	{ -1 };
		GLint		locFaceTint_	{ -1 };
		GLint		locFlags_		{ -1 };

		std::vector<glm::vec4>			materialColours_;
		bool							materialsDirty_	{ false };

		std::vector<SectionRecord>		records_;
		std::vector<DrawCommand>		commands_;
		std::vector<graphics::renderer::DebugVertex>	boundsLines_;
		std::unique_ptr<graphics::renderer::LineRenderer>	lineRenderer_;

		// The arena's CPU-side size no longer matches the GL buffer; render() replaces it.
		bool		arenaResizePending_	{ false };

		FrameStats	stats_;
		bool		initialised_	{ false };
		bool		arenaFullLogged_{ false };
	};
}
