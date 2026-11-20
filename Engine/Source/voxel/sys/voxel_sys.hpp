/*************************************************************************************************************
\file       voxel_sys.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares voxel::System, the voxel terrain subsystem (world lifecycle, meshing, collision, editing
            and streaming), plus its render settings, stats, collider mode and progress types.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Voxel
#include "../types/voxel_types.hpp"
#include "../types/material_table.hpp"
#include "../world/world.hpp"
#include "../mesh/section_mesh.hpp"
#include "../io/voxmap_io.hpp"
#include "../gen/recipe.hpp"
#include "../edit/brush_ops.hpp"
#include "../edit/edit_history.hpp"
#include "../edit/region_transform.hpp"
#include "../edit/voxel_raycast.hpp"
#include "../edit/voxel_region.hpp"

// Event bus
#include "../../events/bus.hpp"

// ECS
#include "../../ecs/ecs_manager/ecs_mng.hpp"

// Threads
#include "../../util/thread/job_queue.hpp"

// GLM
#include <glm/glm.hpp>

// stds
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>


namespace graphics	{ class System; }
namespace physics	{ class System; class CookedMesh; }
namespace vox		{ class ResourceManager; }

namespace voxel
{
	namespace render { class VoxelRenderer; }
	namespace stream { class CellStreamer; class EditStore; struct Settings; }

	class TerrainGenerator;
	struct CellSections;

	/**
	 * Render tuning, loaded from Assets/voxel/render.json and editable
	 * live from the Editor panel.
	 *
	 */
	struct RenderSettings
	{
		float		subDivisionSize_	{ 64.f };			// pixels; descend a LoD node projecting larger than this squared

		// What the Editor sets subDivisionSize_ to when it attaches ("editor"
		// block of render.json). Larger means coarser LoD nearer the camera:
		// an editing session wants headroom more than it wants far detail.
		float		editorSubDivisionSize_	{ 96.f };
		std::size_t	arenaBytes_			{ 64u << 20 };
		std::size_t	uploadBudgetBytes_	{ 8u << 20 };		// per frame
		glm::vec4	faceTint_			{ 1.f, 0.5f, 0.8f, 0.6f };	// up, down, z, x
		bool		ambientOcclusion_	{ false };
		bool		multiDrawIndirect_	{ true };
		bool		wireframe_			{ false };
		bool		lodColours_			{ false };
		bool		sectionBounds_		{ false };
	};

	struct Stats
	{
		std::uint32_t	denseSections_	{ 0 };
		std::uint32_t	uniformSections_{ 0 };
		std::uint32_t	meshedNodes_	{ 0 };
		std::uint32_t	residentQuads_	{ 0 };
		std::uint32_t	drawsLastFrame_	{ 0 };
		std::uint32_t	quadsLastFrame_	{ 0 };
		std::uint32_t	bodies_			{ 0 };
		std::size_t		gpuBytesUsed_	{ 0 };
		std::size_t		gpuBytesCapacity_{ 0 };

		// sectionPool(): every dense section in the process.
		std::size_t		poolBytesInUse_		{ 0 };
		std::size_t		poolBytesReserved_	{ 0 };
		std::size_t		poolBytesPeak_		{ 0 };
		std::uint64_t	poolAllocations_	{ 0 };
		std::uint64_t	poolSlabsCreated_	{ 0 };

		// A streamed ("endless") terrain only; all zero otherwise.
		bool			endless_			{ false };
		std::uint32_t	streamCells_[kLodLevels]{};		// resident cells by the finest level they hold
		std::uint32_t	streamBuilding_		{ 0 };
		float			streamRadiusScale_	{ 1.f };	// below 1 while the memory cap pulls the rings in
		std::size_t		editStoreBytes_		{ 0 };
		std::uint32_t	editStoreSections_	{ 0 };
		std::size_t		tileCacheBytes_		{ 0 };
		std::uint64_t	tileBuilds_			{ 0 };
		std::uint64_t	tileHits_			{ 0 };
		double			generateMs_		{ 0 };
		double			mipMs_			{ 0 };
		double			meshMs_			{ 0 };
	};

	/**
	 * When terrain collision meshes are cooked. Cooking is the slow half of
	 * meshing a section, and only a running simulation needs the result.
	 *
	 */
	enum class ColliderMode : std::uint8_t
	{
		Live,		// with every mesh: the game, and the Editor while it plays
		Deferred	// not at all; sections are noted and cooked on the switch to Live
	};

	struct Progress
	{
		bool		busy_		{ false };
		float		fraction_	{ 0.f };
		std::string	stage_;

		// Why the last load or generate failed; empty unless stage_ is "failed".
		std::string	error_;
	};

	/**
	 * The voxel world subsystem: owns the loaded World, the material
	 * table, the worker pool that generates and meshes it, the renderer
	 * and the terrain collision bodies.
	 *
	 * Threading contract. Workers only ever read the World and write
	 * their own results; everything that touches the ECS, OpenGL or the
	 * bus happens on the main thread inside update() (GL-free) or the
	 * renderer's render() (GL). Edits are applied between mesh batches so
	 * no worker can observe a section changing under it.
	 *
	 * Follows the scene: SceneManager publishes SceneWorldChanged and this
	 * system loads or unloads in response. The initial scene loads before
	 * this system exists, so SystemManager calls syncWorld() once after
	 * init().
	 *
	 */
	class System
	{
	public:

		/*!
			@brief
				Stores the bus, ECS and physics system it works with; nothing is loaded until init().
			@param bus
				The engine event bus
			@param ecs
				The ECS that receives the cpn::VoxelWorld entity
			@param phys
				The physics system that holds the terrain bodies
			*/
		System(::events::Bus& bus, ecs::ECSManager& ecs, physics::System& phys);

		/*!
			@brief
				Shuts the system down.
			*/
		~System();

		System(const System&) = delete;
		System& operator=(const System&) = delete;

		/*!
			@brief
				Loads the material table and render settings, creates the
				renderer (needs a current GL context) and subscribes to the bus.
			@param gph
				The graphics system (unused)
			@param resources
				The resource manager assets are loaded through
			*/
		void init(graphics::System& gph, vox::ResourceManager& resources);

		/*!
			@brief
				Main-thread, GL-free: drains worker results, applies queued
				edits, feeds the renderer and physics, publishes events.
			*/
		void update();

		/*!
			@brief
				Stops workers, removes bodies, frees GL. Idempotent.
			*/
		void shutdown();

		// ==================== World lifecycle ====================

		/*!
			@brief
				Loads (or unloads, for "") whatever the scene currently points at.
			@param mapPath
				Asset-relative .voxmap path, or empty to unload
			*/
		void syncWorld(const std::string& mapPath);

		/*!
			@brief
				Loads a .voxmap asynchronously. A missing file is generated from
				the .voxgen of the same stem and saved.
			@param mapPath
				Asset-relative .voxmap path
			@return
				false if neither the map nor a recipe exists
			*/
		bool loadWorld(const std::string& mapPath);

		/*!
			@brief
				Cancels any build and drops the current world, its bodies, entity and edit state.
			*/
		void unloadWorld();

		/*!
			@brief
				Generates a fresh world from a recipe, asynchronously, replacing
				the current one when done. Cancels a running load or generate.
			@param recipePath
				Asset-relative .voxgen path; throws if it does not exist
			@param seedOverride
				Replaces the recipe's seed when set
			*/
		void generate(const std::string& recipePath, std::optional<std::uint64_t> seedOverride = std::nullopt);

		/*!
			@brief
				The same from a recipe in memory.

				This is how the Editor's Terrain panel generates: it composes the
				recipe from the user's choices (composeBiomeRecipe) and no file is
				involved until the level is saved.
			@param recipe
				The recipe to generate from
			@param label
				What the log and the WorldLoaded event call this world
			*/
		void generate(Recipe recipe, std::string label = "generated terrain");

		/*!
			@brief
				Writes level 0 of the current world, and beside it the recipe the
				world was generated from as <stem>.voxgen.

				Maps are not under version control; the recipe is what lets everyone
				else regenerate the same terrain. The recipe goes first, so the map
				is never the older of the two (loadWorld would regenerate it), and is
				left alone when the file already says the same. Throws
				std::runtime_error.
			@param mapPath
				Asset-relative .voxmap path to write
			*/
		void saveWorld(const std::string& mapPath);

		/*!
			@brief
				Whether a world is installed.
			@return
				true when there is a world
			*/
		bool hasWorld() const { return world_ != nullptr; }

		/*!
			@brief
				Whether the world is a streamed terrain (Recipe::Stream).

				Generated around the focus as it moves, never whole, and never
				written to a .voxmap. Hand edits to it last for the session.
			@return
				true for a streamed world
			*/
		bool endless() const { return world_ && streamer_ != nullptr; }

		/*!
			@brief
				Sets where a streamed terrain is centred: the player, or the camera
				when there is none.

				Ignored by a bounded world. Wired to the bus in
				engine/wiring/event_wiring.cpp; a frame late is soon enough.
			@param position
				World-space focus position
			*/
		void setStreamFocus(const glm::vec3& position) { streamFocus_ = position; }

		/*!
			@brief
				Every mesh resident and every body added.
			@return
				true once the world is ready
			*/
		bool ready() const { return world_ && ready_; }

		/*!
			@brief
				Whether the next update() has nothing to do: no load or generate
				running, no queued edit, no mesh, upload or body outstanding.

				What a frame limiter asks before it slows the loop down - results
				from the workers are only picked up by update().
			@return
				true when nothing is outstanding
			*/
		bool quiet() const;

		// ======================= Collision =======================

		/*!
			@brief
				Sets when terrain collision meshes are cooked.

				Live cooks a collision mesh with every level-0 mesh, as always.
				Deferred cooks none - not for a load, a generate or an edit - and
				keeps the bodies that exist, stale as they may become. Switching
				back to Live cooks every section that was skipped, and only those.
				The Editor sits in Deferred until Play: its brushes aim with the
				voxel raycast, and nothing else touches terrain while paused.
			@param mode
				The new collider mode
			*/
		void setColliderMode(ColliderMode mode);

		/*!
			@brief
				The current collider mode.
			@return
				Live or Deferred
			*/
		ColliderMode colliderMode() const { return colliderMode_; }

		/*!
			@brief
				Whether the terrain's bodies match its voxels: Live, and nothing
				waiting to be cooked, meshed or added. True with no world.
			@return
				true when collision is up to date
			*/
		bool collidersReady() const;

		/*!
			@brief
				Whether the world differs from the file mapPath() names: it was
				generated rather than read, or a voxel was edited since.

				Cleared by a load from disk, saveWorld and unloadWorld.
			@return
				true when the world is modified
			*/
		bool modified() const { return world_ && modified_; }

		/*!
			@brief
				Whether voxels were edited by hand since the world was installed.

				Such a world is no longer what its recipe generates: saving keeps
				the edits in the map, but a map regenerated from the recipe (on a
				machine that does not have the map) will not show them.
			@return
				true when voxels were edited by hand
			*/
		bool edited() const { return world_ && edited_; }

		/*!
			@brief
				The recipe the current world was generated from.
			@return
				The recipe, or null when there is no world, or it was read from a
				map with no readable recipe beside it
			*/
		const Recipe* recipe() const { return world_ && recipe_ ? &*recipe_ : nullptr; }

		// ======================= Queries =========================

		/*!
			@brief
				The material of one voxel.
			@param x
				Voxel x
			@param y
				Voxel y
			@param z
				Voxel z
			@return
				Its material id, or air with no world
			*/
		MaterialId material(int x, int y, int z) const;

		/*!
			@brief
				Queues an edit; applied in the next update() that finds no mesh
				batch in flight, then the section and its LoD ancestors remesh.
			@param x
				Voxel x
			@param y
				Voxel y
			@param z
				Voxel z
			@param id
				Material to write
			@return
				false when there is no world or the voxel is out of range
			*/
		bool setMaterial(int x, int y, int z, MaterialId id);

		/*!
			@brief
				The material table in use.
			@return
				The material table
			*/
		const MaterialTable& materials() const { return materials_; }

		/*!
			@brief
				The world's size in voxels.
			@return
				The logical extent, or zero with no world
			*/
		glm::ivec3 worldSizeVoxels() const;

		/*!
			@brief
				The edge length of one voxel.
			@return
				Metres per voxel (0.1 with no world)
			*/
		float voxelSize() const;

		/*!
			@brief
				The world-space position of voxel (0, 0, 0)'s corner.
			@return
				The world origin, or zero with no world
			*/
		glm::vec3 origin() const;

		/*!
			@brief
				Highest solid voxel in a column, or nothing.
			@param x
				Voxel column x
			@param z
				Voxel column z
			@return
				The y of the highest solid voxel, or nothing
			*/
		std::optional<int> surfaceHeight(int x, int z) const;

		/*!
			@brief
				Voxel behind a physics hit; see voxel_math.hpp.
			@param point
				World-space hit point
			@param normal
				Surface normal at the hit
			@return
				The voxel, or (-1, -1, -1) with no world
			*/
		glm::ivec3 hitToVoxel(const glm::vec3& point, const glm::vec3& normal) const;

		/*!
			@brief
				The cpn::VoxelWorld entity, or entt::null.
			@return
				The world entity
			*/
		entt::entity entity() const { return entity_; }

		/*!
			@brief
				The current world.
			@return
				The world, or null
			*/
		const World* world() const { return world_.get(); }

		/*!
			@brief
				The map path the current world belongs to.
			@return
				Asset-relative .voxmap path, empty when none
			*/
		const std::string& mapPath() const { return mapPath_; }

		/*!
			@brief
				First solid voxel along a ray, read from the grid itself.

				It sees an edit the frame it lands, long before the section's
				collision mesh is rebuilt. Main thread only.
			@param ray
				The ray to cast, in world space
			@param options
				Distance limit, pass-through materials and so on
			@param beforeOpenEdit
				Aim at the terrain as it was when the open transaction began,
				ignoring what the transaction has done since. A brush asks for this
				while its stroke lasts: aimed at the live terrain, a stroke that
				adds ground would hit its own last stamp and climb towards the eye
			@return
				The hit, or nothing
			*/
		std::optional<edit::VoxelHit> raycast(const util::geom::Ray& ray, edit::RaycastOptions options = {}, bool beforeOpenEdit = false) const;

		// ======================== Editing ========================
		//
		//  Everything here only queues. The queue is worked through in
		//  update(), in order, in the same write window setMaterial uses,
		//  and spread over frames when a frame's share of time runs out. So
		//  a brush reads the world as the stamp before it left it, and no
		//  worker ever sees a section change under it.

		/*!
			@brief
				Whether edits can be queued: there is a world and it is not being
				replaced by a load or a generate.
			@return
				true when edits can be queued
			*/
		bool editable() const { return world_ && !pipeline_.joinable(); }

		/*!
			@brief
				Queued edits not applied yet.
			@return
				true while any edit or command is queued
			*/
		bool editsPending() const { return !commands_.empty() || !edits_.empty(); }

		/*!
			@brief
				Whether saveWorld would succeed right now.

				False while the world is building, while queued edits wait, and
				while the meshes of the last edit are still being rebuilt - which,
				unlike a load, progress() does not report as busy.
			@return
				true when a save can go ahead
			*/
		bool canSave() const;

		/*!
			@brief
				Opens a transaction: everything queued until endEdit() is one entry
				in the undo history.

				Edits outside a transaction (setMaterial, the Lua fills, gameplay)
				are applied but not recorded.
			@param label
				The undo entry's name
			@param deferColliders
				Rebuild collision meshes once, at endEdit(), instead of after every
				stamp. Cooking them is the slow half of remeshing a section, and
				nothing collides with terrain mid-stroke in the Editor
			@return
				The transaction's id, for editAlive(); 0 if there is no world
			*/
		edit::History::Id beginEdit(std::string label, bool deferColliders = true);

		/*!
			@brief
				Closes the transaction opened by beginEdit().
			*/
		void endEdit();

		/*!
			@brief
				Undoes what the open transaction has done so far and keeps it open.
			*/
		void rollbackEdit();

		/*!
			@brief
				Applies an op once per stamp, in order.
			@param stamps
				In voxel units; see edit::stampFromWorld
			@param params
				The op and its settings
			*/
		void queueBrush(std::vector<edit::Stamp> stamps, const edit::OpParams& params);

		/*!
			@brief
				Writes a whole region back.
			@param region
				The region to write
			*/
		void queueRegion(edit::VoxelRegion region);

		/*!
			@brief
				Moves a block of terrain: empties the box it was copied from, then
				puts the copy back where placement says.

				Both happen when the queue reaches this entry, so the block lands
				on the terrain as it is then.
			@param source
				A copy of the block taken with readRegion(); shared, because a live
				preview queues the same block again every time its box moves
			@param placement
				Where and how the block is set down
			@param mergeEmpty
				Whether the block's air leaves the terrain under it alone
			*/
		void queueTransform(std::shared_ptr<const edit::VoxelRegion> source, const edit::Placement& placement, bool mergeEmpty);

		/*!
			@brief
				Queues an undo, like any edit, so it keeps its place among strokes.
			*/
		void undoEdit();

		/*!
			@brief
				Queues a redo, like any edit, so it keeps its place among strokes.
			*/
		void redoEdit();

		/*!
			@brief
				Whether a transaction still exists: queued, open, or in the history
				to be undone or redone.

				False once it turned out to change nothing, was evicted for space,
				or the world was replaced.
			@param id
				The id beginEdit() returned
			@return
				true while the transaction exists
			*/
		bool editAlive(edit::History::Id id) const;

		/*!
			@brief
				The undo history.
			@return
				The edit history
			*/
		const edit::History& history() const { return history_; }

		/*!
			@brief
				A copy of a box of the world as it is now; air where there is none.
			@param box
				The voxel box to copy
			@param out
				Receives the copy
			*/
		void readRegion(const edit::VoxelBox& box, edit::VoxelRegion& out) const;

		// ==================== Editor surface =====================

		/*!
			@brief
				The render settings, for editing.
			@return
				The render settings
			*/
		RenderSettings& renderSettings() { return settings_; }

		/*!
			@brief
				The render settings.
			@return
				The render settings
			*/
		const RenderSettings& renderSettings() const { return settings_; }

		/*!
			@brief
				The last refreshed statistics.
			@return
				The stats
			*/
		const Stats& stats() const { return stats_; }

		/*!
			@brief
				A snapshot of the load / generate progress, with the fraction taken
				from the worker counters while busy.
			@return
				The progress
			*/
		Progress progress() const;

		/*!
			@brief
				Asks a running load or generate to stop.
			*/
		void cancel();

		/*!
			@brief
				The voxel renderer.
			@return
				The renderer, or null before init()
			*/
		render::VoxelRenderer* renderer() { return renderer_.get(); }

		/*!
			@brief
				Asset-relative paths of every maps/*.voxgen.
			@param out
				Cleared, then receives the sorted paths
			*/
		void listRecipes(std::vector<std::string>& out) const;

	private:

		// One finished mesh coming back from a worker.
		struct MeshResult
		{
			int										level_{ 0 };
			glm::ivec3								coord_{ 0 };
			SectionMesh								mesh_;
			std::shared_ptr<const physics::CookedMesh>	shape_;	// level 0 only

			// False when the collision mesh was left out on purpose: a null
			// shape_ then means "not cooked", not "nothing to collide with".
			bool									cooked_{ true };
		};

		// One entry of the edit queue.
		struct EditCommand
		{
			enum class Kind : std::uint8_t { Begin, End, Rollback, Brush, Region, Transform, Undo, Redo };

			Kind								kind_			{ Kind::Brush };
			edit::History::Id					id_				{ 0 };		// Begin
			std::string							label_;						// Begin
			bool								deferColliders_	{ false };	// Begin
			std::vector<edit::Stamp>			stamps_;					// Brush
			std::size_t							nextStamp_		{ 0 };		// Brush: resumes here next frame
			edit::OpParams						params_;					// Brush
			std::shared_ptr<const edit::VoxelRegion>	region_;			// Region; Transform: the block
			edit::Placement						placement_;					// Transform
			bool								mergeEmpty_		{ false };	// Transform
		};

		// What one frame's edits have to re-mip and remesh, gathered so the
		// jobs are queued once, after the last write.
		struct DirtySections
		{
			std::vector<glm::ivec3>	changed_;		// voxels differ
			std::vector<glm::ivec3>	neighbours_;	// only their border faces do
		};

		struct Edit
		{
			glm::ivec3	voxel_;
			MaterialId	material_;
		};

		struct PendingBody
		{
			std::shared_ptr<const physics::CookedMesh>	shape_;
			float										friction_{ 0.6f };
		};

		/*!
			@brief
				Bus handler for SceneWorldChanged; forwards to syncWorld().
			@param mapPath
				The scene's new map path
			*/
		void onSceneWorldChanged(const std::string& mapPath);

		/*!
			@brief
				Reads Assets/voxel/render.json into the render settings, keeping defaults on error.
			@param resources
				The resource manager to load through
			*/
		void loadRenderSettings(vox::ResourceManager& resources);

		/*!
			@brief
				Cancels and joins the pipeline thread, if any.
			*/
		void stopPipeline();

		/*!
			@brief
				Joins the pipeline thread once it has flagged itself finished.
			*/
		void reapPipeline();

		/*!
			@brief
				Replaces the current world with a built one and starts meshing it.
			@param world
				The new world
			@param label
				The name logged and given to the entity and the WorldLoaded event
			*/
		void installWorld(std::shared_ptr<World> world, std::string label);

		/*!
			@brief
				Both generate() overloads: source runs on the pipeline thread and may throw.
			@param source
				Produces the recipe to generate from
			@param label
				The name the generated world gets
			*/
		void startGenerate(std::function<Recipe()> source, std::string label);

		/*!
			@brief
				Queues a mesh job for every non-air section at every level, coarse first.
			*/
		void meshAll();

		/*!
			@brief
				Queues a worker job that meshes one section (and cooks its collider at level 0).
			@param level
				LoD level
			@param coord
				Section coordinate at that level
			@param epoch
				The world epoch the result belongs to
			@param cook
				Whether a level-0 job also cooks the collision mesh
			*/
		void enqueueMesh(int level, const glm::ivec3& coord, std::uint64_t epoch, bool cook = true);

		/*!
			@brief
				Takes a finished mesh on the main thread: rebuilds its body and hands it to the renderer.
			@param r
				The worker's result
			@param epoch
				The epoch the job was queued in; stale results are dropped
			*/
		void onMeshResult(MeshResult&& r, std::uint64_t epoch);

		/*!
			@brief
				Applies the queued single-voxel edits and notes what they dirtied.
			@param dirty
				Receives the changed sections and their border neighbours
			*/
		void applyEdits(DirtySections& dirty);

		/*!
			@brief
				Works through the edit command queue within the frame's edit budget.
			@param dirty
				Receives the changed sections and their border neighbours
			*/
		void runCommands(DirtySections& dirty);

		/*!
			@brief
				Records a section's state before its first change in the open transaction.
			@param section
				Level-0 section coordinate
			@param before
				The section as it is before the write
			*/
		void rememberBefore(const glm::ivec3& section, const Section& before);

		/*!
			@brief
				Marks whole swapped-in sections, and every face neighbour, dirty.
			@param sections
				The sections an undo, redo or rollback put back
			@param dirty
				Receives them
			*/
		void restored(const std::vector<glm::ivec3>& sections, DirtySections& dirty);

		/*!
			@brief
				Re-mips the ancestors of the frame's changes and queues their remeshes.
			@param dirty
				What the frame's edits changed
			*/
		void commitDirty(DirtySections& dirty);

		/*!
			@brief
				Drops queued edits, commands, the undo history and deferred-collider state.
			*/
		void clearEditState();

		/*!
			@brief
				Whether a level-0 mesh queued now should come with its collision mesh.
			@return
				true in Live mode outside a deferring transaction
			*/
		bool cookNow() const { return colliderMode_ == ColliderMode::Live && !deferColliders_; }

		/*!
			@brief
				Queues a cooking mesh job for every section in staleColliders_.
			*/
		void cookStaleColliders();

		// ---- Streaming (voxel_sys_stream.cpp) ----

		struct MeshRequest
		{
			int			level_	{ 0 };
			glm::ivec3	coord_	{ 0 };
			bool		cook_	{ false };
		};

		/*!
			@brief
				Replaces the world with a streamed one. Main thread. Throws what the recipe or a material gets wrong.
			@param recipe
				An endless recipe
			@param label
				The name the world gets
			@param generated
				Whether it was generated rather than read from disk (the recipe is not saved yet)
			*/
		void startStream(Recipe recipe, std::string label, bool generated);

		/*!
			@brief
				Stops the stream's workers and forgets its kept edits.
			*/
		void stopStream();

		/*!
			@brief
				Per frame, outside the write window: focus, memory cap, new builds.
			*/
		void streamSchedule();

		/*!
			@brief
				Inside the write window: lets cells go, then puts finished ones in.
			@param meshes
				Receives the mesh jobs the new cells need
			*/
		void streamApply(std::vector<MeshRequest>& meshes);

		/*!
			@brief
				Drops a cell's pages from one detail level up to another, keeping its hand edits.
			@param cell
				The cell
			@param from
				The finest level to drop
			@param to
				The detail the cell keeps (levels below it go)
			*/
		void streamEvict(const glm::ivec2& cell, int from, int to);

		/*!
			@brief
				Installs a built cell's pages and requests its meshes.
			@param cell
				The worker-built sections; moved from
			@param meshes
				Receives the mesh jobs
			*/
		void streamInstall(CellSections& cell, std::vector<MeshRequest>& meshes);

		/*!
			@brief
				Cooks the terrain near the focus and drops the bodies that fell behind.
			@param meshes
				Receives the cooking mesh jobs
			*/
		void streamColliders(std::vector<MeshRequest>& meshes);

		/*!
			@brief
				Removes every body, placed or pending, of one cell.
			@param cell
				The cell
			*/
		void removeCellBodies(const glm::ivec2& cell);

		/*!
			@brief
				Whether a streamed world has what its first frame needs.
			@return
				true once the detail-0 ring is in and, when Live, its collision
			*/
		bool streamReady() const;

		/*!
			@brief
				Adds a mesh request for every non-air section of a cell at one level.
			@param cell
				The cell
			@param level
				LoD level
			@param cook
				Whether the jobs cook collision meshes
			@param meshes
				Receives the requests
			*/
		void requestCellMeshes(const glm::ivec2& cell, int level, bool cook, std::vector<MeshRequest>& meshes) const;

		/*!
			@brief
				Removes a section's body and queues a new one when there is a shape.
			@param coord
				Level-0 section coordinate
			@param mesh
				The section's mesh, for its friction
			@param shape
				The cooked collision mesh, or null for none
			*/
		void rebuildBody(const glm::ivec3& coord, const SectionMesh& mesh, std::shared_ptr<const physics::CookedMesh> shape);

		/*!
			@brief
				Removes every terrain body, placed or pending.
			*/
		void removeAllBodies();

		/*!
			@brief
				Sets the progress state under its mutex.
			@param busy
				Whether work is running
			@param fraction
				Progress, 0 to 1
			@param stage
				The stage name
			@param error
				Why it failed, when stage is "failed"
			*/
		void setProgress(bool busy, float fraction, const char* stage, std::string error = {});

		/*!
			@brief
				Updates the stats and profiler plots; the section walk runs every 30 frames.
			*/
		void refreshStats();

		/*!
			@brief
				The number of terrain bodies in the physics system.
			@return
				The body count
			*/
		std::uint32_t bodyCount() const;

		::events::Bus&			bus_;
		ecs::ECSManager&		ecs_;
		physics::System&		phys_;
		vox::ResourceManager*	resources_	{ nullptr };

		MaterialTable			materials_;
		RenderSettings			settings_;
		Stats					stats_;
		voxmap::Info			lastInfo_;

		std::shared_ptr<World>					world_;
		std::unique_ptr<render::VoxelRenderer>	renderer_;
		std::string								mapPath_;
		entt::entity							entity_		{ entt::null };

		// Workers and the pipeline thread that orchestrates them.
		util::thread::JobQueue			jobs_;
		util::thread::MainThreadQueue	completions_;
		std::thread						pipeline_;
		std::atomic<bool>				pipelineFinished_{ false };
		std::atomic<bool>				cancel_		{ false };
		std::atomic<std::uint64_t>		epoch_		{ 0 };		// bumped on every load/generate/unload
		std::atomic<int>				progressCount_{ 0 };
		std::atomic<int>				progressTotal_{ 0 };

		mutable std::mutex				progressMutex_;
		Progress						progress_;

		// Meshing bookkeeping (main thread).
		std::size_t						meshesExpected_	{ 0 };
		std::size_t						meshesArrived_	{ 0 };
		bool							ready_			{ false };
		bool							readyPublished_	{ false };
		bool							modified_		{ false };
		bool							edited_			{ false };
		std::optional<Recipe>			recipe_;
		std::vector<Edit>				edits_;

		// The edit queue, its undo history, and the level-0 sections whose
		// collision mesh was skipped - while a transaction deferred them, or
		// because the mode is Deferred - and is owed once cookNow() holds.
		std::deque<EditCommand>			commands_;
		edit::History					history_;
		edit::History::Id				nextEditId_		{ 1 };
		bool							deferColliders_	{ false };
		ColliderMode					colliderMode_	{ ColliderMode::Live };
		std::vector<glm::ivec3>			staleColliders_;
		int								bodiesSinceOptimize_{ 0 };

		// Every section the open transaction has changed, as it was before:
		// what raycast(..., beforeOpenEdit) reads. Uncompressed, unlike the
		// history's copy, because a brush casts a ray through it every frame.
		edit::SectionOverrides			editBase_;

		// Level-0 section index -> body id, for the sections that have one. A
		// map, not a slot per section: a streamed world's grid is millions of
		// sections, of which a few thousand near the player have bodies.
		std::unordered_map<std::size_t, std::uint32_t>	bodies_;
		std::deque<std::pair<glm::ivec3, PendingBody>>	pendingBodies_;

		::events::ScopedSubscription	sceneSub_;
		bool							initialised_	{ false };

		// Frames until refreshStats() walks the sections again.
		int								statsCountdown_	{ 0 };

		// A streamed terrain: the scheduler and its workers, the hand edits of
		// cells that have streamed out, the level-0 sections edited while
		// resident (what an eviction has to keep), and the cells near enough
		// the focus to have collision.
		std::unique_ptr<stream::CellStreamer>	streamer_;
		std::unique_ptr<stream::EditStore>		editStore_;
		std::unordered_set<std::size_t>			editedSections_;
		std::unordered_set<std::uint64_t>		collidableCells_;
		glm::vec3								streamFocus_		{ 0.f };
		int										capCooldown_		{ 0 };
		bool									capLogged_			{ false };
		bool									editOutOfReachLogged_{ false };
		int										streamLogTicks_		{ 0 };
		std::uint64_t							streamLoggedBuilds_	{ 0 };
		std::size_t								streamLoggedBodies_	{ 0 };
	};
}
