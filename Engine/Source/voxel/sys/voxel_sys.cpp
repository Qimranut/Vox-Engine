/*************************************************************************************************************
\file       voxel_sys.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      voxel::System: world load / generate / save on the pipeline thread, meshing on the workers,
            terrain bodies, the per-frame write window, the edit queue and undo, and the Editor-facing
            queries. The streamed-terrain half is in voxel_sys_stream.cpp.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "voxel_sys.hpp"

// Voxel
#include "../events/voxel_events.hpp"
#include "../gen/recipe.hpp"
#include "../gen/terrain_generator.hpp"
#include "../io/voxmap_io.hpp"
#include "../mesh/section_mesher.hpp"
#include "../physics/terrain_collider.hpp"
#include "../render/voxel_renderer.hpp"
#include "../stream/cell_streamer.hpp"
#include "../stream/edit_store.hpp"
#include "../world/voxel_math.hpp"

// Scene
#include "../../scene/scene_events.hpp"

// Physics
#include "../../physics/sys/phy_sys.hpp"
#include "../../physics/shape/cooked_mesh.hpp"

// Components
#include "../../components/component_types/voxel_world.hpp"

// Resources
#include "../../resources/ResourceManager.hpp"

// JSON
#include <nlohmann/json.hpp>

// Profiling
#include "../../profile/profiler.hpp"

// stds
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>


namespace voxel
{
	namespace
	{
		constexpr int kBodiesPerFrame = 64;

		// Bodies added since the broad phase was last rebuilt before it is
		// rebuilt again.
		constexpr int kOptimizeAfterBodies = 256;

		// Empty slabs of the section pool kept across an unload (2 MiB each).
		constexpr std::size_t kPoolSlabsKept = 16;

		/*!
			@brief
				Orders section coordinates by y, then z, then x.
			@param a
				First coordinate
			@param b
				Second coordinate
			@return
				true when a sorts before b
			*/
		bool sectionLess(const glm::ivec3& a, const glm::ivec3& b)
		{
			return a.y != b.y ? a.y < b.y : a.z != b.z ? a.z < b.z : a.x < b.x;
		}

		/*!
			@brief
				Sorts a list of section coordinates and removes duplicates.
			@param sections
				The list, changed in place
			*/
		void uniqueSections(std::vector<glm::ivec3>& sections)
		{
			std::sort(sections.begin(), sections.end(), sectionLess);
			sections.erase(std::unique(sections.begin(), sections.end()), sections.end());
		}

		// A plot is identified by the address of its name, so the series and
		// its configuration share this one constant.
		constexpr const char* kPlotArenaUsed = "Voxel GPU arena used";
		constexpr const char* kPlotPoolUsed = "Voxel section pool used";

		/*!
			@brief
				Reads a whole file; throws std::runtime_error when it cannot be opened.
			@param path
				The file to read
			@return
				Its bytes
			*/
		std::vector<unsigned char> readAllBytes(const std::filesystem::path& path)
		{
			std::ifstream in(path, std::ios::binary);
			if (!in)
				throw std::runtime_error("cannot open " + path.string());
			return std::vector<unsigned char>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
		}

		/*!
			@brief
				Writes a recipe unless the file already parses to the same one.

				So a hand-written recipe keeps its layout and its timestamp. Through
				a .tmp and a rename, like the map.
			@param file
				The .voxgen file to write
			@param recipe
				The recipe to write
			@return
				Whether the file was written
			*/
		bool writeRecipeIfChanged(const std::filesystem::path& file, const Recipe& recipe)
		{
			if (std::filesystem::exists(file))
			{
				try
				{
					if (Recipe::parse(readAllBytes(file)) == recipe)
						return false;
				}
				catch (const std::exception&)
				{
					// Unreadable: replace it.
				}
			}

			std::filesystem::path tmp = file;
			tmp += ".tmp";
			{
				std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
				if (!out)
					throw std::runtime_error("cannot write " + tmp.string());
				out << recipe.toJson();
				if (!out.flush())
					throw std::runtime_error("cannot write " + tmp.string());
			}
			std::filesystem::rename(tmp, file);
			return true;
		}

		/*!
			@brief
				Milliseconds elapsed since a time point.
			@param t
				The start time
			@return
				Elapsed milliseconds
			*/
		double msSince(std::chrono::steady_clock::time_point t)
		{
			return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
		}
	}

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
	System::System(::events::Bus& bus, ecs::ECSManager& ecs, physics::System& phys)
		:
		bus_{ bus },
		ecs_{ ecs },
		phys_{ phys }
	{}

	/*!
		@brief
			Shuts the system down.
		*/
	System::~System()
	{
		shutdown();
	}

	// ================================ Lifecycle ================================

	/*!
		@brief
			Loads the material table (falling back to the built-in one) and render
			settings, creates the renderer and subscribes to SceneWorldChanged.

			Needs a current GL context. Does nothing when already initialised.
		@param gph
			The graphics system (unused)
		@param resources
			The resource manager assets are loaded through
		*/
	void System::init(graphics::System& /*gph*/, vox::ResourceManager& resources)
	{
		if (initialised_)
			return;

		VOX_PROFILE_FUNCTION_C(profile::colour::kVoxel);
		VOX_PROFILE_PLOT_CONFIG(kPlotArenaUsed, VOX_PROFILE_PLOT_MEMORY);
		VOX_PROFILE_PLOT_CONFIG(kPlotPoolUsed, VOX_PROFILE_PLOT_MEMORY);

		resources_ = &resources;

		try
		{
			materials_ = MaterialTable::parse(resources.load("voxel/materials.json")->bytes);
		}
		catch (const std::exception& e)
		{
			std::cerr << "[voxel] " << e.what() << " - using the built-in material table\n";
			materials_ = MaterialTable::builtin();
		}

		loadRenderSettings(resources);

		renderer_ = std::make_unique<render::VoxelRenderer>();
		renderer_->init(settings_);
		renderer_->setMaterials(materials_);

		sceneSub_ = ::events::ScopedSubscription(bus_, bus_.subscribe<vox::events::SceneWorldChanged>(
			[this](const vox::events::SceneWorldChanged& e) { onSceneWorldChanged(e.world_); }));

		initialised_ = true;
		std::cout << "[voxel] initialised with " << materials_.size() << " materials, " << jobs_.workerCount() << " workers\n";
	}

	/*!
		@brief
			Reads Assets/voxel/render.json into the render settings, keeping defaults on error.
		@param resources
			The resource manager to load through
		*/
	void System::loadRenderSettings(vox::ResourceManager& resources)
	{
		try
		{
			const vox::ResourceHandle file = resources.load("voxel/render.json");
			const nlohmann::json doc = nlohmann::json::parse(file->bytes.begin(), file->bytes.end());

			settings_.subDivisionSize_ = doc.value("subDivisionSize", settings_.subDivisionSize_);
			settings_.arenaBytes_ = static_cast<std::size_t>(doc.value("quadArenaMiB", 64)) << 20;
			settings_.uploadBudgetBytes_ = static_cast<std::size_t>(doc.value("uploadBudgetMiBPerFrame", 8)) << 20;
			settings_.ambientOcclusion_ = doc.value("ambientOcclusion", false);
			settings_.multiDrawIndirect_ = doc.value("multiDrawIndirect", true);

			if (const auto editor = doc.find("editor"); editor != doc.end())
				settings_.editorSubDivisionSize_ = editor->value("subDivisionSize", settings_.editorSubDivisionSize_);

			// Debug views; the Editor panel toggles the same flags live.
			if (const auto dbg = doc.find("debug"); dbg != doc.end())
			{
				settings_.wireframe_ = dbg->value("wireframe", false);
				settings_.lodColours_ = dbg->value("lodColours", false);
				settings_.sectionBounds_ = dbg->value("sectionBounds", false);
			}

			if (const auto t = doc.find("faceTint"); t != doc.end())
			{
				settings_.faceTint_ = {
					t->value("up", 1.0f), t->value("down", 0.5f), t->value("z", 0.8f), t->value("x", 0.6f) };
			}
		}
		catch (const std::exception& e)
		{
			std::cerr << "[voxel] render.json: " << e.what() << " - using defaults\n";
		}
	}

	/*!
		@brief
			Stops workers, removes bodies, frees GL, destroys the world entity and trims the pool. Idempotent.
		*/
	void System::shutdown()
	{
		if (!initialised_)
			return;

		sceneSub_.reset();
		stopPipeline();
		stopStream();
		jobs_.waitIdle();
		completions_.clear();

		removeAllBodies();

		if (renderer_)
		{
			renderer_->shutdown();
			renderer_.reset();
		}

		if (entity_ != entt::null && ecs_.registry().valid(entity_))
			ecs_.registry().destroy(entity_);
		entity_ = entt::null;

		world_.reset();
		clearEditState();
		sectionPool().trim();
		initialised_ = false;
	}

	/*!
		@brief
			Bus handler for SceneWorldChanged; forwards to syncWorld().
		@param mapPath
			The scene's new map path
		*/
	void System::onSceneWorldChanged(const std::string& mapPath)
	{
		syncWorld(mapPath);
	}

	/*!
		@brief
			Loads (or unloads, for "") whatever the scene currently points at.

			The same path is skipped only while the world is unmodified; a path
			with neither a map nor a recipe unloads the current world and reports
			the failure through progress().
		@param mapPath
			Asset-relative .voxmap path, or empty to unload
		*/
	void System::syncWorld(const std::string& mapPath)
	{
		if (!initialised_)
			return;

		if (mapPath.empty())
		{
			if (hasWorld() || pipeline_.joinable())
				unloadWorld();
			return;
		}

		// A modified world is not the file any more, so a scene asking for
		// that file again - reopened after discarding changes - reloads it.
		if (mapPath == mapPath_ && hasWorld() && !modified_)
			return;

		if (!loadWorld(mapPath))
		{
			// Showing the previous scene's terrain under a scene that asked
			// for something else would be worse than showing none.
			if (hasWorld() || pipeline_.joinable())
				unloadWorld();

			std::cerr << "[voxel] scene references \"" << mapPath << "\" but neither it nor a .voxgen of that name exists\n";
			setProgress(false, 0.f, "failed", "\"" + mapPath + "\" does not exist, and neither does a .voxgen of that name");
		}
	}

	// ============================= Load / generate ============================

	/*!
		@brief
			Cancels and joins the pipeline thread, if any, then clears the cancel and finished flags.
		*/
	void System::stopPipeline()
	{
		cancel_.store(true);
		if (pipeline_.joinable())
			pipeline_.join();
		cancel_.store(false);
		pipelineFinished_.store(false);
	}

	/*!
		@brief
			Joins the pipeline thread once it has flagged itself finished.
		*/
	void System::reapPipeline()
	{
		// The pipeline thread flags itself finished on exit; join it here so
		// joinable() means "still building" everywhere else.
		if (pipeline_.joinable() && pipelineFinished_.load())
		{
			pipeline_.join();
			pipelineFinished_.store(false);
		}
	}

	namespace
	{
		// Sets a flag when the pipeline thread's body exits, exceptions included.
		struct FinishFlag
		{
			std::atomic<bool>& flag_;

			/*!
				@brief
					Sets the flag.
				*/
			~FinishFlag() { flag_.store(true); }
		};
	}

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
	void System::setProgress(bool busy, float fraction, const char* stage, std::string error)
	{
		std::lock_guard<std::mutex> lock(progressMutex_);
		progress_.busy_ = busy;
		progress_.fraction_ = fraction;
		progress_.stage_ = stage;
		progress_.error_ = std::move(error);
	}

	/*!
		@brief
			A snapshot of the progress, with the fraction taken from the worker counters while busy.
		@return
			The progress
		*/
	Progress System::progress() const
	{
		std::lock_guard<std::mutex> lock(progressMutex_);
		Progress p = progress_;
		const int total = progressTotal_.load();
		if (p.busy_ && total > 0)
			p.fraction_ = std::clamp(static_cast<float>(progressCount_.load()) / static_cast<float>(total), 0.f, 1.f);
		return p;
	}

	/*!
		@brief
			Asks a running load or generate to stop.
		*/
	void System::cancel()
	{
		cancel_.store(true);
	}

	/*!
		@brief
			Loads a .voxmap asynchronously on the pipeline thread.

			A missing map, or one older than the .voxgen beside it, is generated
			from that recipe and saved. An endless recipe starts a stream instead.
		@param mapPath
			Asset-relative .voxmap path
		@return
			false if neither the map nor a recipe exists
		*/
	bool System::loadWorld(const std::string& mapPath)
	{
		if (!initialised_ || !resources_)
			return false;

		const std::filesystem::path file = resources_->resolve(mapPath);
		std::filesystem::path recipe = file;
		recipe.replace_extension(".voxgen");

		bool haveMap = std::filesystem::exists(file);
		const bool haveRecipe = std::filesystem::exists(recipe);
		if (!haveMap && !haveRecipe)
			return false;

		// A map older than the recipe beside it is a stale cook, the same
		// rule cooked textures follow. A map saved from the Editor is newer
		// than its recipe, so edits are never regenerated away.
		if (haveMap && haveRecipe)
		{
			std::error_code ec;
			const auto mapTime = std::filesystem::last_write_time(file, ec);
			const auto recipeTime = ec ? mapTime : std::filesystem::last_write_time(recipe, ec);
			if (!ec && recipeTime > mapTime)
			{
				std::cout << "[voxel] " << mapPath << " is older than its recipe; regenerating\n";
				haveMap = false;
			}
		}

		stopPipeline();
		const std::uint64_t epoch = ++epoch_;
		mapPath_ = mapPath;
		setProgress(true, 0.f, haveMap ? "reading map" : "generating from recipe");
		progressCount_ = 0;
		progressTotal_ = 0;

		const std::string recipeRel = resources_->relative(recipe);

		pipeline_ = std::thread([this, epoch, file, recipe, recipeRel, haveMap, mapPath]
		{
			profile::nameThread("Voxel pipeline");
			VOX_PROFILE_SCOPE_C("Load world", profile::colour::kVoxel);

			const FinishFlag finished{ pipelineFinished_ };
			try
			{
				std::shared_ptr<World> world;
				std::optional<Recipe> source;
				voxmap::Info info;
				const auto t0 = std::chrono::steady_clock::now();

				// The recipe speaks first: an endless terrain has no map, and
				// one left beside it from before it was made endless is not it.
				if (std::filesystem::exists(recipe))
				{
					std::optional<Recipe> endless;
					try
					{
						Recipe r = Recipe::parse(readAllBytes(recipe));
						if (r.stream_.endless_)
							endless = std::move(r);
					}
					catch (const std::exception&)
					{
						// Reported below, by whichever branch needs the recipe.
					}

					if (endless)
					{
						completions_.post([this, epoch, mapPath, r = std::move(*endless)]() mutable
						{
							if (epoch != epoch_.load())
								return;

							try
							{
								startStream(std::move(r), mapPath, false);
								mapPath_ = mapPath;
							}
							catch (const std::exception& e)
							{
								std::cerr << "[voxel] load failed: " << e.what() << "\n";
								setProgress(false, 0.f, "failed", std::string("load failed: ") + e.what());
							}
						});
						return;
					}
				}

				if (haveMap)
				{
					voxmap::ReadResult rr = voxmap::read(readAllBytes(file), materials_);
					for (const std::string& w : rr.warnings_)
						std::cerr << "[voxel] " << mapPath << ": " << w << "\n";
					world = std::move(rr.world_);
					info = rr.info_;

					// The recipe beside the map is what it was generated from. One
					// that does not parse only means saving will not rewrite it.
					if (std::filesystem::exists(recipe))
					{
						try { source = Recipe::parse(readAllBytes(recipe)); }
						catch (const std::exception& e) { std::cerr << "[voxel] " << recipeRel << ": " << e.what() << "\n"; }
					}
				}
				else
				{
					const Recipe r = Recipe::parse(readAllBytes(recipe));
					const TerrainGenerator generator(r, materials_);
					world = std::make_shared<World>(generator.dims());
					progressTotal_ = generator.workUnits();
					if (!generator.generate(*world, &jobs_, &progressCount_, &cancel_))
						return;
					info.seed_ = r.seed_;
					info.recipePath_ = recipeRel;
					info.materialHash_ = materials_.hash();

					setProgress(true, 1.f, "saving map");
					voxmap::writeFile(file, *world, materials_, info);
					std::cout << "[voxel] generated and saved " << mapPath << "\n";
					source = r;
				}

				const double loadMs = msSince(t0);
				if (cancel_.load())
					return;

				setProgress(true, 1.f, "building LoD");
				const auto t1 = std::chrono::steady_clock::now();
				world->rebuildLevels(materials_, &jobs_);
				const double mipMs = msSince(t1);

				if (cancel_.load())
					return;

				completions_.post([this, epoch, world, mapPath, info, loadMs, mipMs, source = std::move(source)]() mutable
				{
					if (epoch != epoch_.load())
						return;
					stats_.generateMs_ = loadMs;
					stats_.mipMs_ = mipMs;
					lastInfo_ = info;
					installWorld(std::move(world), mapPath);
					recipe_ = std::move(source);
					modified_ = false;
				});
			}
			catch (const std::exception& e)
			{
				const std::string what = e.what();
				completions_.post([this, epoch, what]
				{
					if (epoch != epoch_.load())
						return;
					std::cerr << "[voxel] load failed: " << what << "\n";
					setProgress(false, 0.f, "failed", "load failed: " + what);
				});
			}
		});

		return true;
	}

	/*!
		@brief
			Generates a fresh world from a recipe file, asynchronously.
		@param recipePath
			Asset-relative .voxgen path; throws if it does not exist
		@param seedOverride
			Replaces the recipe's seed when set
		*/
	void System::generate(const std::string& recipePath, std::optional<std::uint64_t> seedOverride)
	{
		if (!initialised_ || !resources_)
			return;

		const std::filesystem::path recipeFile = resources_->resolve(recipePath);
		if (!std::filesystem::exists(recipeFile))
			throw std::runtime_error("recipe not found: " + recipePath);

		startGenerate([recipeFile, seedOverride]
		{
			Recipe r = Recipe::parse(readAllBytes(recipeFile));
			if (seedOverride)
				r.seed_ = *seedOverride;
			return r;
		}, recipePath);
	}

	/*!
		@brief
			Generates a fresh world from a recipe in memory, asynchronously.
		@param recipe
			The recipe to generate from
		@param label
			What the log and the WorldLoaded event call this world
		*/
	void System::generate(Recipe recipe, std::string label)
	{
		if (!initialised_ || !resources_)
			return;

		startGenerate([recipe = std::move(recipe)] { return recipe; }, std::move(label));
	}

	/*!
		@brief
			Both generate() overloads: runs source on the pipeline thread, generates and mips the
			world, then installs it on the main thread (or starts a stream for an endless recipe).
		@param source
			Produces the recipe to generate from; may throw
		@param label
			The name the generated world gets
		*/
	void System::startGenerate(std::function<Recipe()> source, std::string label)
	{
		stopPipeline();
		const std::uint64_t epoch = ++epoch_;
		setProgress(true, 0.f, "generating");
		progressCount_ = 0;
		progressTotal_ = 0;

		pipeline_ = std::thread([this, epoch, source = std::move(source), label = std::move(label)]
		{
			profile::nameThread("Voxel pipeline");
			VOX_PROFILE_SCOPE_C("Generate world", profile::colour::kVoxel);

			const FinishFlag finished{ pipelineFinished_ };
			try
			{
				Recipe r = source();

				// Never made whole: it is streamed in from here on.
				if (r.stream_.endless_)
				{
					completions_.post([this, epoch, label, r = std::move(r)]() mutable
					{
						if (epoch != epoch_.load())
							return;

						try
						{
							startStream(std::move(r), label, true);
						}
						catch (const std::exception& e)
						{
							std::cerr << "[voxel] generate failed: " << e.what() << "\n";
							setProgress(false, 0.f, "failed", std::string("generate failed: ") + e.what());
						}
					});
					return;
				}

				const TerrainGenerator generator(r, materials_);
				auto world = std::make_shared<World>(generator.dims());
				progressTotal_ = generator.workUnits();

				const auto t0 = std::chrono::steady_clock::now();
				if (!generator.generate(*world, &jobs_, &progressCount_, &cancel_))
					return;
				const double genMs = msSince(t0);

				setProgress(true, 1.f, "building LoD");
				const auto t1 = std::chrono::steady_clock::now();
				world->rebuildLevels(materials_, &jobs_);
				const double mipMs = msSince(t1);

				if (cancel_.load())
					return;

				completions_.post([this, epoch, world, label, r = std::move(r), genMs, mipMs]() mutable
				{
					if (epoch != epoch_.load())
						return;
					stats_.generateMs_ = genMs;
					stats_.mipMs_ = mipMs;
					lastInfo_.seed_ = r.seed_;
					lastInfo_.recipePath_ = label;
					lastInfo_.materialHash_ = materials_.hash();
					installWorld(std::move(world), label);
					recipe_ = std::move(r);

					// Nothing on disk holds this world until saveWorld.
					modified_ = true;
				});
			}
			catch (const std::exception& e)
			{
				const std::string what = e.what();
				completions_.post([this, epoch, what]
				{
					if (epoch != epoch_.load())
						return;
					std::cerr << "[voxel] generate failed: " << what << "\n";
					setProgress(false, 0.f, "failed", "generate failed: " + what);
				});
			}
		});
	}
	/*!
		@brief
			Replaces the current world with a built one: clears bodies, stream and
			edit state, updates the entity and renderer, publishes WorldLoaded and
			starts meshing a bounded world.
		@param world
			The new world
		@param label
			The name logged and given to the entity and the WorldLoaded event
		*/
	void System::installWorld(std::shared_ptr<World> world, std::string label)
	{
		VOX_PROFILE_FUNCTION_C(profile::colour::kVoxel);

		removeAllBodies();
		stopStream();

		// The history's sections belong to the world being replaced.
		clearEditState();
		ready_ = false;
		readyPublished_ = false;
		edited_ = false;
		recipe_.reset();

		world_ = std::move(world);
		pendingBodies_.clear();

		auto& registry = ecs_.registry();
		if (entity_ == entt::null || !registry.valid(entity_))
			entity_ = registry.create();
		registry.emplace_or_replace<cpn::VoxelWorld>(entity_, cpn::VoxelWorld{ label });

		if (renderer_)
			renderer_->setWorld(world_);

		const WorldDims& d = world_->dims();
		std::cout << "[voxel] loaded " << label << " (" << d.voxels_.x << "x" << d.voxels_.y << "x" << d.voxels_.z
			<< " voxels, " << world_->denseCount(0) << " dense sections)\n";

		bus_.publish(events::WorldLoaded{ label, d.voxels_ });

		// A streamed world starts empty and is meshed a cell at a time.
		if (world_->storage() == World::Storage::Sparse)
		{
			meshesExpected_ = meshesArrived_ = 0;
			return;
		}

		setProgress(true, 0.f, "meshing");
		meshAll();
	}

	/*!
		@brief
			Cancels any build and drops the current world, its bodies, entity and
			edit state; publishes WorldUnloaded if there was one.
		*/
	void System::unloadWorld()
	{
		stopPipeline();
		++epoch_;
		completions_.clear();

		removeAllBodies();
		stopStream();
		clearEditState();
		pendingBodies_.clear();
		meshesExpected_ = meshesArrived_ = 0;
		ready_ = false;

		if (renderer_)
			renderer_->setWorld(nullptr);

		auto& registry = ecs_.registry();
		if (entity_ != entt::null && registry.valid(entity_))
			registry.destroy(entity_);
		entity_ = entt::null;

		const bool had = world_ != nullptr;
		world_.reset();
		mapPath_.clear();
		modified_ = false;
		edited_ = false;
		recipe_.reset();
		setProgress(false, 0.f, "idle");

		// Hand the world's slabs back, but for a few: the usual reason to
		// unload is that another world is on its way. A mesh job still running
		// keeps the old world alive, and with it the slabs it has blocks in.
		sectionPool().trim(kPoolSlabsKept);
		stats_ = Stats{};
		statsCountdown_ = 0;

		if (had)
		{
			std::cout << "[voxel] world unloaded\n";
			bus_.publish(events::WorldUnloaded{});
		}
	}

	/*!
		@brief
			Writes the recipe sidecar, then level 0 of the world to the map.

			A streamed world writes only its recipe. Throws std::runtime_error
			when there is no world or it is still being built.
		@param mapPath
			Asset-relative .voxmap path to write
		*/
	void System::saveWorld(const std::string& mapPath)
	{
		VOX_PROFILE_FUNCTION_C(profile::colour::kVoxel);

		if (!world_ || !resources_)
			throw std::runtime_error("no voxel world to save");

		// A streamed terrain is its recipe: there is no map to write, and what
		// the player or a brush did to it is not kept past the session.
		if (streamer_)
		{
			if (!recipe_)
				throw std::runtime_error("the streamed terrain has no recipe to save");

			std::filesystem::path sidecar = resources_->resolve(mapPath);
			sidecar.replace_extension(".voxgen");
			if (writeRecipeIfChanged(sidecar, *recipe_))
				std::cout << "[voxel] saved " << resources_->relative(sidecar) << "\n";

			// A map left beside it from before the terrain was endless is left
			// alone: loadWorld reads the recipe first and never opens it.
			mapPath_ = mapPath;
			modified_ = false;
			return;
		}

		if (jobs_.pending() != 0 || pipeline_.joinable())
			throw std::runtime_error("the voxel world is still being built - try again in a moment");

		const std::filesystem::path file = resources_->resolve(mapPath);
		voxmap::Info info = lastInfo_;
		info.materialHash_ = materials_.hash();

		if (recipe_)
		{
			std::filesystem::path sidecar = file;
			sidecar.replace_extension(".voxgen");
			if (writeRecipeIfChanged(sidecar, *recipe_))
				std::cout << "[voxel] saved " << resources_->relative(sidecar) << "\n";
			info.recipePath_ = resources_->relative(sidecar);
		}

		voxmap::writeFile(file, *world_, materials_, info);
		mapPath_ = mapPath;
		modified_ = false;
		std::cout << "[voxel] saved " << mapPath << "\n";
	}

	// ================================ Meshing =================================

	/*!
		@brief
			Queues a mesh job for every non-air section at every level, coarse first.
		*/
	void System::meshAll()
	{
		VOX_PROFILE_FUNCTION_C(profile::colour::kVoxel);

		const std::uint64_t epoch = epoch_.load();
		meshesExpected_ = 0;
		meshesArrived_ = 0;

		const bool cook = cookNow();

		// Coarse levels first so a distant view has something to draw while
		// the fine sections stream in.
		for (int level = kLodLevels - 1; level >= 0; --level)
		{
			const glm::ivec3 d = world_->sectionsAt(level);
			for (int sy = 0; sy < d.y; ++sy)
			for (int sz = 0; sz < d.z; ++sz)
			for (int sx = 0; sx < d.x; ++sx)
			{
				if (world_->section(level, sx, sy, sz).isAir())
					continue;
				enqueueMesh(level, { sx, sy, sz }, epoch, cook);

				if (level == 0 && !cook)
					staleColliders_.push_back({ sx, sy, sz });
			}
		}

		progressTotal_ = static_cast<int>(meshesExpected_);
		progressCount_ = 0;
	}

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
	void System::enqueueMesh(int level, const glm::ivec3& coord, std::uint64_t epoch, bool cook)
	{
		++meshesExpected_;

		// The job holds its own reference to the world, so an unload while
		// it runs cannot pull the sections out from under it.
		std::shared_ptr<const World> world = world_;
		const bool ao = settings_.ambientOcclusion_;
		const float voxelSize = world_->dims().voxelSize_;

		jobs_.push([this, world, level, coord, epoch, ao, voxelSize, cook]
		{
			if (epoch != epoch_.load())
				return;

			VOX_PROFILE_SCOPE_C("Mesh section", profile::colour::kVoxel);
			VOX_PROFILE_VALUE(level);

			MeshResult r;
			r.level_ = level;
			r.coord_ = coord;
			r.mesh_ = meshSection(gatherMeshInput(*world, level, coord), materials_, ao);
			r.cooked_ = cook;
			if (level == 0 && cook)
				r.shape_ = cookSectionCollider(r.mesh_, voxelSize);

			completions_.post([this, r = std::move(r), epoch]() mutable { onMeshResult(std::move(r), epoch); });
		});
	}

	/*!
		@brief
			Takes a finished mesh on the main thread: rebuilds its body and hands it to the renderer.
		@param r
			The worker's result
		@param epoch
			The epoch the job was queued in; stale results are dropped
		*/
	void System::onMeshResult(MeshResult&& r, std::uint64_t epoch)
	{
		if (epoch != epoch_.load() || !world_)
			return;

		++meshesArrived_;
		progressCount_.fetch_add(1);

		// A streamed cell can be let go between a job finishing and its result
		// being drained here. Taking the mesh would mark the node meshed, and
		// the renderer would descend into a hole.
		if (!world_->sectionResident(r.level_, r.coord_.x, r.coord_.y, r.coord_.z))
			return;

		// A mesh whose collider was deferred keeps the body it has: stale for
		// the length of a stroke, and replaced when the stroke ends.
		if (r.level_ == 0 && r.cooked_)
			rebuildBody(r.coord_, r.mesh_, std::move(r.shape_));

		if (renderer_)
			renderer_->submitMesh(r.level_, r.coord_, std::move(r.mesh_));
	}

	// ================================ Physics =================================

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
	void System::rebuildBody(const glm::ivec3& coord, const SectionMesh& mesh, std::shared_ptr<const physics::CookedMesh> shape)
	{
		const std::size_t index = world_->sectionIndex(0, coord.x, coord.y, coord.z);

		if (const auto body = bodies_.find(index); body != bodies_.end())
		{
			phys_.removeBody(body->second);
			bodies_.erase(body);
		}

		std::erase_if(pendingBodies_, [&](const auto& p) { return p.first == coord; });

		if (shape)
			pendingBodies_.emplace_back(coord, PendingBody{ std::move(shape), sectionFriction(mesh, materials_) });
	}

	/*!
		@brief
			Removes every terrain body, placed or pending.
		*/
	void System::removeAllBodies()
	{
		for (const auto& [index, id] : bodies_)
			phys_.removeBody(id);

		bodies_.clear();
		pendingBodies_.clear();
	}

	// ================================= Update =================================

	/*!
		@brief
			Main-thread, GL-free: drains worker results, applies queued edits,
			feeds the renderer and physics, publishes events.

			Edits and stream installs run only in the write window, when no mesh
			job is pending and no pipeline is running; bodies are added in batches.
		*/
	void System::update()
	{
		if (!initialised_)
			return;

		VOX_PROFILE_FUNCTION_C(profile::colour::kVoxel);

		reapPipeline();

		{
			VOX_PROFILE_SCOPE("Drain worker results");

			// Never inside the macro: it expands to nothing without the profiler.
			const std::size_t drained = completions_.drain();
			VOX_PROFILE_VALUE(drained);
			(void)drained;
		}

		if (!world_)
			return;

		// Edits wait for a quiet moment so no worker sees a section change.
		// Every write of the frame lands first and the jobs are queued once,
		// after the last one: a job queued in between would read the next write.
		//
		// A streamed world shares the window: cells are let go and put in here
		// too, after the edits and before anything is meshed.
		if (streamer_)
			streamSchedule();

		if ((editsPending() || streamer_) && jobs_.pending() == 0 && !pipeline_.joinable())
		{
			DirtySections dirty;
			applyEdits(dirty);
			runCommands(dirty);

			std::vector<MeshRequest> meshes;
			if (streamer_)
				streamApply(meshes);

			commitDirty(dirty);

			const std::uint64_t epoch = epoch_.load();
			for (const MeshRequest& m : meshes)
				enqueueMesh(m.level_, m.coord_, epoch, m.cook_);
		}

		// Bodies in batches: adding thousands in one frame would hitch.
		int added = 0;
		while (!pendingBodies_.empty() && added < kBodiesPerFrame)
		{
			VOX_PROFILE_SCOPE("Add terrain body");

			auto [coord, body] = std::move(pendingBodies_.front());
			pendingBodies_.pop_front();

			if (!world_->sectionResident(0, coord.x, coord.y, coord.z))
				continue;

			const std::size_t index = world_->sectionIndex(0, coord.x, coord.y, coord.z);
			glm::vec3 mn, mx;
			sectionBounds(world_->dims(), 0, coord, mn, mx);

			const std::uint32_t id = phys_.addStaticMesh(*body.shape_, mn, entity_, body.friction_);
			if (id != physics::kInvalidBodyId)
				bodies_[index] = id;
			++added;
		}
		bodiesSinceOptimize_ += added;

		const bool meshesDone = meshesArrived_ >= meshesExpected_;
		const bool resident = renderer_ ? renderer_->allResident() : true;
		// A streamed world is never "finished": it is ready once the ground
		// round the focus is in, and stays ready while the rest comes and goes.
		const bool settled = meshesDone && pendingBodies_.empty() && resident && jobs_.pending() == 0;
		const bool nowReady = streamer_ ? (readyPublished_ || (settled && streamReady())) : settled;

		// Bodies added one at a time leave the broad phase's tree lopsided.
		// Rebuilding it is worth it after a load, and again whenever a batch
		// this size has gone in since: colliders cooked on Play, streamed cells.
		if (nowReady && readyPublished_ && bodiesSinceOptimize_ >= kOptimizeAfterBodies)
		{
			phys_.optimizeBroadPhase();
			bodiesSinceOptimize_ = 0;

			// Once per Play in the Editor; a streamed world does this all the time.
			if (!streamer_)
				std::cout << "[voxel] collision caught up: " << bodyCount() << " bodies\n";
		}

		if (nowReady && !ready_)
		{
			ready_ = true;
			if (!readyPublished_)
			{
				readyPublished_ = true;
				phys_.optimizeBroadPhase();
				bodiesSinceOptimize_ = 0;
				setProgress(false, 1.f, "ready");
				std::cout << "[voxel] world ready: " << meshesArrived_ << " meshes, " << bodyCount() << " bodies\n";
				bus_.publish(events::WorldReady{});
			}
		}
		else if (!nowReady)
		{
			ready_ = false;
		}

		refreshStats();
	}

	// =============================== Collision ================================

	/*!
		@brief
			Sets when terrain collision meshes are cooked; switching to Live cooks the skipped sections.
		@param mode
			The new collider mode
		*/
	void System::setColliderMode(ColliderMode mode)
	{
		if (mode == colliderMode_)
			return;

		colliderMode_ = mode;

		// An open transaction still defers; its own end catches up (commitDirty).
		if (world_ && cookNow())
			cookStaleColliders();
	}

	/*!
		@brief
			Queues a cooking mesh job for every section in staleColliders_.
		*/
	void System::cookStaleColliders()
	{
		if (staleColliders_.empty())
			return;

		VOX_PROFILE_FUNCTION_C(profile::colour::kVoxel);

		uniqueSections(staleColliders_);

		// Queued at once, without waiting for the write window: a job only
		// reads, and no write happens while one is pending. The quads are not
		// kept once uploaded, so the section is meshed again to be cooked.
		const std::uint64_t epoch = epoch_.load();
		std::size_t queued = 0;

		for (const glm::ivec3& c : staleColliders_)
		{
			// Emptied since; its body went with its mesh.
			if (world_->section(0, c.x, c.y, c.z).isAir())
				continue;

			// A streamed world has collision near the focus only, and its
			// cells are cooked whole as they come into range (streamColliders).
			if (streamer_)
			{
				const glm::ivec2 cell = World::cellOf(0, c.x, c.z);
				const std::uint64_t key = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cell.y)) << 32) | static_cast<std::uint32_t>(cell.x);
				if (collidableCells_.count(key) == 0)
					continue;
			}

			enqueueMesh(0, c, epoch, true);
			++queued;
		}

		staleColliders_.clear();
		ready_ = false;

		std::cout << "[voxel] cooking " << queued << " terrain collision meshes\n";
	}

	/*!
		@brief
			Whether the terrain's bodies match its voxels. True with no world.
		@return
			true when collision is up to date
		*/
	bool System::collidersReady() const
	{
		if (pipeline_.joinable())
			return false;
		if (!world_)
			return true;

		// Streamed: the ground near the focus, which is all that ever has collision.
		if (streamer_)
			return cookNow() && streamReady() && pendingBodies_.empty() && meshesArrived_ >= meshesExpected_;

		return cookNow() && staleColliders_.empty() && pendingBodies_.empty()
			&& meshesArrived_ >= meshesExpected_ && jobs_.pending() == 0 && !editsPending();
	}

	/*!
		@brief
			Whether the next update() has nothing to do.
		@return
			true when nothing is outstanding
		*/
	bool System::quiet() const
	{
		if (pipeline_.joinable() || jobs_.pending() != 0)
			return false;

		// No world and nothing building one.
		if (!world_)
			return true;

		if (streamer_ && (!streamer_->idle() || meshesArrived_ < meshesExpected_ || !pendingBodies_.empty()
			|| (renderer_ && !renderer_->allResident())))
			return false;

		return ready_ && !editsPending();
	}

	// ================================= Editing ================================

	namespace
	{
		// How long one frame may spend applying queued brush stamps. What does
		// not fit waits for the next quiet frame; a stroke catches up rather
		// than stalling the editor.
		constexpr double kEditBudgetMs = 6.0;

		constexpr glm::ivec3 kFaceSteps[6] = { { -1, 0, 0 }, { 1, 0, 0 }, { 0, -1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 } };
	}

	/*!
		@brief
			Applies the queued single-voxel edits and notes what they dirtied.
		@param dirty
			Receives the changed sections and their border neighbours
		*/
	void System::applyEdits(DirtySections& dirty)
	{
		if (edits_.empty())
			return;

		VOX_PROFILE_FUNCTION_C(profile::colour::kVoxel);

		std::vector<Edit> edits;
		edits.swap(edits_);

		for (const Edit& e : edits)
		{
			if (!world_->inBounds(e.voxel_.x, e.voxel_.y, e.voxel_.z) || world_->get(e.voxel_.x, e.voxel_.y, e.voxel_.z) == e.material_)
				continue;

			const glm::ivec3 s = e.voxel_ >> kSectionShift;
			const glm::ivec3 l = e.voxel_ & kSectionMask;
			if (history_.isOpen())
				rememberBefore(s, world_->section(0, s.x, s.y, s.z));

			world_->set(e.voxel_.x, e.voxel_.y, e.voxel_.z, e.material_);
			dirty.changed_.push_back(s);

			// Neighbours sharing the touched plane must remesh too: their
			// boundary faces depend on this voxel.
			for (int a = 0; a < 3; ++a)
			{
				if (l[a] == 0) dirty.neighbours_.push_back(s + kFaceSteps[2 * a]);
				if (l[a] == kSectionMask) dirty.neighbours_.push_back(s + kFaceSteps[2 * a + 1]);
			}

			// A long run of single-voxel edits lands in few sections.
			if (dirty.changed_.size() > 4096)
			{
				uniqueSections(dirty.changed_);
				uniqueSections(dirty.neighbours_);
			}
		}

		world_->takeDirtyLevel0();
	}

	/*!
		@brief
			Records a section's state before its first change in the open transaction,
			in the history and in editBase_.
		@param section
			Level-0 section coordinate
		@param before
			The section as it is before the write
		*/
	void System::rememberBefore(const glm::ivec3& section, const Section& before)
	{
		if (!history_.isOpen())
			return;

		history_.touch(section, before);

		// Only the first state seen: later writes of the same stroke find the
		// stroke's own work there.
		const std::size_t index = world_->sectionIndex(0, section.x, section.y, section.z);
		if (!editBase_.contains(index))
			editBase_.emplace(index, before.clone());
	}

	/*!
		@brief
			Marks whole swapped-in sections, and every face neighbour, dirty.
		@param sections
			The sections an undo, redo or rollback put back
		@param dirty
			Receives them
		*/
	void System::restored(const std::vector<glm::ivec3>& sections, DirtySections& dirty)
	{
		// A whole section was swapped, so any of its borders may have changed.
		for (const glm::ivec3& s : sections)
		{
			dirty.changed_.push_back(s);
			for (const glm::ivec3& step : kFaceSteps)
				dirty.neighbours_.push_back(s + step);
		}
	}

	/*!
		@brief
			Works through the edit command queue in order within the frame's edit
			budget; a brush that runs out of time resumes next frame.
		@param dirty
			Receives the changed sections and their border neighbours
		*/
	void System::runCommands(DirtySections& dirty)
	{
		if (commands_.empty())
			return;

		VOX_PROFILE_FUNCTION_C(profile::colour::kVoxel);

		const auto started = std::chrono::steady_clock::now();
		const edit::BeforeChange touch = [this](const glm::ivec3& section, const Section& before) { rememberBefore(section, before); };
		const auto written = [&dirty](edit::WriteResult&& result)
		{
			dirty.changed_.insert(dirty.changed_.end(), result.changed_.begin(), result.changed_.end());
			dirty.neighbours_.insert(dirty.neighbours_.end(), result.neighbours_.begin(), result.neighbours_.end());
		};

		edit::VoxelRegion scratch;

		// A streamed world can only be edited where level 0 is resident, which
		// is round the focus. Further out a read would find air where there is
		// ground not yet streamed in, and a brush that smooths or grows would
		// work from that; a write would have nowhere to go. Such an edit is
		// dropped whole.
		const auto reachable = [this](const edit::VoxelBox& box)
		{
			if (!streamer_ || world_->voxelsResident(box.min_, box.max_))
				return true;

			if (!editOutOfReachLogged_)
			{
				editOutOfReachLogged_ = true;
				std::cerr << "[voxel] an edit reached terrain that is not streamed in at full detail and was dropped "
					"(edits work within the first radius of Assets/voxel/stream.json)\n";
			}
			return false;
		};

		while (!commands_.empty() && msSince(started) < kEditBudgetMs)
		{
			EditCommand& c = commands_.front();

			switch (c.kind_)
			{
			case EditCommand::Kind::Begin:
				history_.begin(c.id_, std::move(c.label_));
				deferColliders_ = c.deferColliders_;
				break;

			case EditCommand::Kind::End:
				history_.commit(*world_);
				deferColliders_ = false;
				editBase_.clear();
				break;

			case EditCommand::Kind::Rollback:
				// The world is what it was before again, so there is no "before" to keep.
				restored(history_.rollbackOpen(*world_), dirty);
				editBase_.clear();
				break;

			case EditCommand::Kind::Undo:
				restored(history_.undo(*world_), dirty);
				break;

			case EditCommand::Kind::Redo:
				restored(history_.redo(*world_), dirty);
				break;

			case EditCommand::Kind::Region:
				if (reachable(c.region_->box()))
					written(edit::write(*world_, *c.region_, touch));
				break;

			case EditCommand::Kind::Transform:
			{
				// Lift the block out, then set it down: the target is read
				// after the lift, so a block moved a little lands on the hole
				// it left and not on a copy of itself.
				if (!reachable(c.region_->box()) || !reachable(edit::placedBounds(c.region_->box(), c.placement_)))
					break;

				written(edit::write(*world_, edit::VoxelRegion(c.region_->box(), kAir), touch));

				edit::read(*world_, edit::placedBounds(c.region_->box(), c.placement_), scratch);
				if (edit::place(scratch, *c.region_, c.placement_, c.mergeEmpty_) != 0)
					written(edit::write(*world_, scratch, touch));
				break;
			}

			case EditCommand::Kind::Brush:
				// Each stamp reads the world as the one before it left it.
				while (c.nextStamp_ < c.stamps_.size())
				{
					const edit::Stamp& stamp = c.stamps_[c.nextStamp_++];
					if (!reachable(edit::contextBox(stamp, c.params_)))
						continue;

					edit::read(*world_, edit::contextBox(stamp, c.params_), scratch);
					if (edit::apply(scratch, stamp, c.params_) != 0)
						written(edit::write(*world_, scratch, touch));

					if (msSince(started) >= kEditBudgetMs)
						break;
				}
				if (c.nextStamp_ < c.stamps_.size())
					return;
				break;
			}

			commands_.pop_front();
		}
	}

	/*!
		@brief
			Marks the world edited, re-mips the ancestors of the frame's changes,
			queues their remeshes (plus owed colliders) and publishes SectionChanged.
		@param dirty
			What the frame's edits changed
		*/
	void System::commitDirty(DirtySections& dirty)
	{
		VOX_PROFILE_FUNCTION_C(profile::colour::kVoxel);

		uniqueSections(dirty.changed_);

		if (!dirty.changed_.empty())
		{
			edited_ = true;

			// A bounded world is its map file, and the map is now out of date.
			// A streamed one is its recipe, which an edit does not change: the
			// edits are remembered instead, for when their cell streams out.
			if (streamer_)
			{
				for (const glm::ivec3& c : dirty.changed_)
					editedSections_.insert(world_->sectionIndex(0, c.x, c.y, c.z));
			}
			else
			{
				modified_ = true;
			}
		}

		// Ancestors first, synchronously and each one once: the renderer will
		// not descend into a child its parent's mask does not list.
		const std::vector<std::pair<int, glm::ivec3>> coarse = world_->remipAncestors(dirty.changed_, materials_);

		// Level 0 remeshes what changed and what borders a changed plane.
		// Coarse levels seal their borders, so there only ancestors do.
		std::vector<glm::ivec3> fine = dirty.changed_;
		for (const glm::ivec3& n : dirty.neighbours_)
			if (world_->validSection(0, n.x, n.y, n.z))
				fine.push_back(n);

		// A transaction that deferred its colliders has closed: catch them up.
		// Not while the mode is Deferred; setColliderMode(Live) does that.
		const bool cook = cookNow();
		if (cook && !staleColliders_.empty())
		{
			fine.insert(fine.end(), staleColliders_.begin(), staleColliders_.end());
			staleColliders_.clear();
		}
		uniqueSections(fine);

		const std::uint64_t epoch = epoch_.load();
		const auto remesh = [&](int level, const glm::ivec3& c, bool cook)
		{
			if (world_->section(level, c.x, c.y, c.z).isAir())
			{
				// Emptied out: drop its mesh and body without a job.
				if (renderer_)
					renderer_->submitMesh(level, c, SectionMesh{});
				if (level == 0)
					rebuildBody(c, SectionMesh{}, nullptr);
				return false;
			}
			enqueueMesh(level, c, epoch, cook);
			return true;
		};

		for (const auto& [level, c] : coarse)
			remesh(level, c, true);

		for (const glm::ivec3& c : fine)
		{
			if (remesh(0, c, cook) && !cook)
				staleColliders_.push_back(c);
		}
		if (!cook)
			uniqueSections(staleColliders_);

		for (const glm::ivec3& c : dirty.changed_)
			bus_.publish(events::SectionChanged{ c });
	}

	/*!
		@brief
			Drops queued edits, commands, the undo history and deferred-collider state.
		*/
	void System::clearEditState()
	{
		edits_.clear();
		commands_.clear();
		history_.clear();
		staleColliders_.clear();
		editBase_.clear();
		deferColliders_ = false;
	}

	/*!
		@brief
			Whether saveWorld would succeed right now.
		@return
			true when a save can go ahead
		*/
	bool System::canSave() const
	{
		// A streamed world saves as its recipe, which no mesh job or queued
		// edit can change - and its mesh jobs never stop for long.
		if (streamer_)
			return world_ && !pipeline_.joinable();

		return world_ && !pipeline_.joinable() && jobs_.pending() == 0 && !editsPending();
	}

	/*!
		@brief
			Queues the opening of a transaction.
		@param label
			The undo entry's name
		@param deferColliders
			Rebuild collision meshes once, at endEdit(), instead of after every stamp
		@return
			The transaction's id; 0 if edits cannot be queued
		*/
	edit::History::Id System::beginEdit(std::string label, bool deferColliders)
	{
		if (!editable())
			return 0;

		EditCommand c;
		c.kind_ = EditCommand::Kind::Begin;
		c.id_ = nextEditId_++;
		c.label_ = std::move(label);
		c.deferColliders_ = deferColliders;
		commands_.push_back(std::move(c));
		return commands_.back().id_;
	}

	/*!
		@brief
			Queues the closing of the open transaction.
		*/
	void System::endEdit()
	{
		if (!editable())
			return;

		EditCommand c;
		c.kind_ = EditCommand::Kind::End;
		commands_.push_back(std::move(c));
	}

	/*!
		@brief
			Queues a rollback of the open transaction, which stays open.
		*/
	void System::rollbackEdit()
	{
		if (!editable())
			return;

		EditCommand c;
		c.kind_ = EditCommand::Kind::Rollback;
		commands_.push_back(std::move(c));
	}

	/*!
		@brief
			Queues an op to be applied once per stamp, in order.
		@param stamps
			In voxel units; see edit::stampFromWorld
		@param params
			The op and its settings; an unknown material drops the command
		*/
	void System::queueBrush(std::vector<edit::Stamp> stamps, const edit::OpParams& params)
	{
		if (!editable() || stamps.empty() || params.material_ >= materials_.size())
			return;

		EditCommand c;
		c.kind_ = EditCommand::Kind::Brush;
		c.stamps_ = std::move(stamps);
		c.params_ = params;
		commands_.push_back(std::move(c));
	}

	/*!
		@brief
			Queues writing a whole region back.
		@param region
			The region to write; an empty one is ignored
		*/
	void System::queueRegion(edit::VoxelRegion region)
	{
		if (!editable() || region.volume() == 0)
			return;

		EditCommand c;
		c.kind_ = EditCommand::Kind::Region;
		c.region_ = std::make_shared<const edit::VoxelRegion>(std::move(region));
		commands_.push_back(std::move(c));
	}

	/*!
		@brief
			Queues moving a block of terrain: empty its source box, then place the copy.
		@param source
			A copy of the block taken with readRegion()
		@param placement
			Where and how the block is set down
		@param mergeEmpty
			Whether the block's air leaves the terrain under it alone
		*/
	void System::queueTransform(std::shared_ptr<const edit::VoxelRegion> source, const edit::Placement& placement, bool mergeEmpty)
	{
		if (!editable() || !source || source->volume() == 0)
			return;

		EditCommand c;
		c.kind_ = EditCommand::Kind::Transform;
		c.region_ = std::move(source);
		c.placement_ = placement;
		c.mergeEmpty_ = mergeEmpty;
		commands_.push_back(std::move(c));
	}

	/*!
		@brief
			Queues an undo, like any edit, so it keeps its place among strokes.
		*/
	void System::undoEdit()
	{
		if (!editable())
			return;

		EditCommand c;
		c.kind_ = EditCommand::Kind::Undo;
		commands_.push_back(std::move(c));
	}

	/*!
		@brief
			Queues a redo, like any edit, so it keeps its place among strokes.
		*/
	void System::redoEdit()
	{
		if (!editable())
			return;

		EditCommand c;
		c.kind_ = EditCommand::Kind::Redo;
		commands_.push_back(std::move(c));
	}

	/*!
		@brief
			Whether a transaction is still queued, open, or in the history.
		@param id
			The id beginEdit() returned
		@return
			true while the transaction exists
		*/
	bool System::editAlive(edit::History::Id id) const
	{
		if (id == 0 || !world_)
			return false;
		if (history_.openId() == id || history_.contains(id))
			return true;

		return std::any_of(commands_.begin(), commands_.end(), [id](const EditCommand& c)
		{
			return c.kind_ == EditCommand::Kind::Begin && c.id_ == id;
		});
	}

	/*!
		@brief
			A copy of a box of the world as it is now; air where there is none.
		@param box
			The voxel box to copy
		@param out
			Receives the copy
		*/
	void System::readRegion(const edit::VoxelBox& box, edit::VoxelRegion& out) const
	{
		if (world_)
			edit::read(*world_, box, out);
		else
			out.reset(box, kAir);
	}

	/*!
		@brief
			First solid voxel along a ray, read from the grid itself. Main thread only.
		@param ray
			The ray to cast, in world space
		@param options
			Distance limit, pass-through materials and so on
		@param beforeOpenEdit
			Read the sections the open transaction changed as they were before it
		@return
			The hit, or nothing
		*/
	std::optional<edit::VoxelHit> System::raycast(const util::geom::Ray& ray, edit::RaycastOptions options, bool beforeOpenEdit) const
	{
		if (!world_)
			return std::nullopt;
		if (beforeOpenEdit)
			options.overrides_ = &editBase_;
		return edit::raycast(*world_, ray, options);
	}

	/*!
		@brief
			Updates the stats and profiler plots; the section walk (and stream
			stats and log line) runs every 30 frames.
		*/
	void System::refreshStats()
	{
		// Both walk every level-0 section; a panel does not need them fresher
		// than twice a second.
		if (statsCountdown_-- <= 0)
		{
			statsCountdown_ = 30;

			const std::size_t dense = world_ ? world_->denseCount(0) : 0;
			stats_.denseSections_ = static_cast<std::uint32_t>(dense);
			stats_.uniformSections_ = world_ ? static_cast<std::uint32_t>(world_->sectionCount(0) - dense) : 0;
			stats_.bodies_ = bodyCount();
		}

		// Every dense section in the process, all LoD levels and the undo
		// copies included - not only this world's level 0.
		const util::memory::BlockPool::Stats pool = sectionPool().stats();
		stats_.poolBytesInUse_ = pool.bytesInUse();
		stats_.poolBytesReserved_ = pool.bytesReserved();
		stats_.poolBytesPeak_ = pool.blocksPeak_ * pool.blockBytes_;
		stats_.poolAllocations_ = pool.allocations_;
		stats_.poolSlabsCreated_ = pool.slabsCreated_;

		stats_.endless_ = streamer_ != nullptr;
		if (streamer_ && statsCountdown_ == 30)		// with the section walk above
		{
			const stream::CellStreamer::Stats s = streamer_->stats();
			std::copy(s.cells_.begin(), s.cells_.end(), std::begin(stats_.streamCells_));
			stats_.streamBuilding_ = s.building_;
			stats_.streamRadiusScale_ = s.radiusScale_;
			stats_.tileCacheBytes_ = s.tileBytes_;
			stats_.tileBuilds_ = s.tileBuilds_;
			stats_.tileHits_ = s.tileHits_;
			stats_.editStoreBytes_ = editStore_->bytes();
			stats_.editStoreSections_ = static_cast<std::uint32_t>(editStore_->size());

			// A line now and then, while cells are coming and going: the one
			// place the log shows that memory stays put as the player travels.
			if (++streamLogTicks_ >= 20 && (s.building_ != 0 || s.tileBuilds_ != streamLoggedBuilds_ || bodies_.size() != streamLoggedBodies_))
			{
				streamLogTicks_ = 0;
				streamLoggedBuilds_ = s.tileBuilds_;
				streamLoggedBodies_ = bodies_.size();
				std::cout << "[voxel] stream: cells " << s.cells_[0] << " / " << s.cells_[1] << " / " << s.cells_[2] << " / " << s.cells_[3]
					<< " by detail, pool " << (pool.bytesInUse() >> 20) << " MiB, " << s.tiles_ << " tiles (" << (s.tileBytes_ >> 20)
					<< " MiB, " << s.tileBuilds_ << " built), " << bodies_.size() << " bodies, " << stats_.meshedNodes_ << " meshes, GPU "
					<< (stats_.gpuBytesUsed_ >> 20) << " MiB" << (s.radiusScale_ < 1.f ? ", rings pulled in" : "") << "\n";
			}
		}

		VOX_PROFILE_PLOT(kPlotPoolUsed, static_cast<std::int64_t>(pool.bytesInUse()));

		if (renderer_)
		{
			const render::FrameStats& f = renderer_->frameStats();
			stats_.meshedNodes_ = f.residentNodes_;
			stats_.residentQuads_ = f.residentQuads_;
			stats_.drawsLastFrame_ = f.draws_;
			stats_.quadsLastFrame_ = f.quads_;
			stats_.gpuBytesUsed_ = f.arenaUsedBytes_;
			stats_.gpuBytesCapacity_ = f.arenaBytes_;

			// Plotted against the frame timeline: a hitch that lines up with
			// an upload burst or a draw-count jump explains itself.
			VOX_PROFILE_PLOT("Voxel draws", static_cast<std::int64_t>(f.draws_));
			VOX_PROFILE_PLOT("Voxel quads drawn", static_cast<std::int64_t>(f.quads_));
			VOX_PROFILE_PLOT("Voxel uploads pending", static_cast<std::int64_t>(f.uploadsPending_));
			VOX_PROFILE_PLOT(kPlotArenaUsed, static_cast<std::int64_t>(f.arenaUsedBytes_));
		}

		VOX_PROFILE_PLOT("Voxel jobs pending", static_cast<std::int64_t>(jobs_.pending()));
	}

	/*!
		@brief
			The number of terrain bodies in the physics system.
		@return
			The body count
		*/
	std::uint32_t System::bodyCount() const
	{
		return static_cast<std::uint32_t>(bodies_.size());
	}

	// ================================= Queries ================================

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
	MaterialId System::material(int x, int y, int z) const
	{
		return world_ ? world_->get(x, y, z) : kAir;
	}

	/*!
		@brief
			Queues a single-voxel edit for the next write window.
		@param x
			Voxel x
		@param y
			Voxel y
		@param z
			Voxel z
		@param id
			Material to write
		@return
			false when there is no world, the voxel is out of range or the material unknown
		*/
	bool System::setMaterial(int x, int y, int z, MaterialId id)
	{
		if (!world_ || !world_->inBounds(x, y, z) || id >= materials_.size())
			return false;

		edits_.push_back(Edit{ { x, y, z }, id });
		return true;
	}

	/*!
		@brief
			The world's size in voxels.
		@return
			The logical extent, or zero with no world
		*/
	glm::ivec3 System::worldSizeVoxels() const
	{
		return world_ ? world_->dims().voxels_ : glm::ivec3{ 0 };
	}

	/*!
		@brief
			The edge length of one voxel.
		@return
			Metres per voxel (0.1 with no world)
		*/
	float System::voxelSize() const
	{
		return world_ ? world_->dims().voxelSize_ : 0.1f;
	}

	/*!
		@brief
			The world-space position of voxel (0, 0, 0)'s corner.
		@return
			The world origin, or zero with no world
		*/
	glm::vec3 System::origin() const
	{
		return world_ ? world_->dims().origin_ : glm::vec3{ 0.f };
	}

	/*!
		@brief
			Highest solid voxel in a column, scanning down from the top.
		@param x
			Voxel column x
		@param z
			Voxel column z
		@return
			The y of the highest solid voxel, or nothing
		*/
	std::optional<int> System::surfaceHeight(int x, int z) const
	{
		if (!world_ || !world_->inBounds(x, 0, z))
			return std::nullopt;

		for (int y = world_->dims().voxels_.y - 1; y >= 0; --y)
		{
			if (world_->get(x, y, z) != kAir)
				return y;
		}
		return std::nullopt;
	}

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
	glm::ivec3 System::hitToVoxel(const glm::vec3& point, const glm::vec3& normal) const
	{
		if (!world_)
			return glm::ivec3{ -1 };
		return voxel::hitToVoxel(world_->dims(), point, normal);
	}

	/*!
		@brief
			Asset-relative paths of every maps/*.voxgen, sorted.
		@param out
			Cleared, then receives the paths
		*/
	void System::listRecipes(std::vector<std::string>& out) const
	{
		out.clear();
		if (!resources_)
			return;

		const std::filesystem::path dir = resources_->root() / "maps";
		std::error_code ec;
		if (!std::filesystem::exists(dir, ec))
			return;

		for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
		{
			if (entry.is_regular_file() && entry.path().extension() == ".voxgen")
				out.push_back(resources_->relative(entry.path()));
		}
		std::sort(out.begin(), out.end());
	}
}
