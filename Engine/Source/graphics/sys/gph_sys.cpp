/*************************************************************************************************************
\file       gph_sys.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      graphics::System: GL 4.5 check and resource creation, camera update through the active
            controller, the entity and world passes, and periodic collection of unused textures.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "gph_sys.hpp"
#include "graphics/text/text_renderer.hpp"

// Graphics
#include "../debug/gl_debug.hpp"
#include "../texture/sampler.hpp"
#include "../texture/texture.hpp"
#include "../texture/texture_manager.hpp"


// Components
#include "../../components/component_types/render_material.hpp"

// Profiling
#include "../../profile/profiler_gpu.hpp"

// stds
#include <stdexcept>
#include <cmath>


namespace graphics
{
	namespace
	{
		// About ten seconds at 60 Hz. Often enough to return memory after a
		// scene change, rare enough that the registry scan never shows up.
		constexpr unsigned kTextureCollectInterval = 600;
	}

	/*!
		@brief
			Stores the window and scene; no GL work happens until init().
		@param window
			Window system whose context is drawn to
		@param ecs
			Scene to draw, read only
		*/
	System::System(window::System& window, const ecs::ECSManager& ecs)
		:
		window_(window),
		ecs_(ecs)
	{}

	/*!
		@brief
			Detaches the GL debug callback if init() ran; GL objects go with the owned members.
		*/
	System::~System()
	{
		if (initialized_)
			debug::disableGlDebugOutput();
	}

	/*!
		@brief
			Creates every GL resource: debug output, GPU profiling context, samplers, textures,
			renderer, mesh shader and cube model, then sets up the camera and parks the orbit rig.

			Throws std::runtime_error if the context is not OpenGL 4.5.
		@param resources
			Decodes texture files; must outlive this system
		*/
	void System::init(vox::ResourceManager& resources)
	{
		// GLEW's aggregate 4.5 flag also requires robustness entry points that
		// we do not use. Check the actual context version instead.
		GLint major = 0, minor = 0;
		glGetIntegerv(GL_MAJOR_VERSION, &major);
		glGetIntegerv(GL_MINOR_VERSION, &minor);
		if (major < 4 || (major == 4 && minor < 5))
			throw std::runtime_error("OpenGL 4.5 is required; the current context is "
				+ std::to_string(major) + "." + std::to_string(minor));

		initialized_ = true;

		debug::enableGlDebugOutput();

		// GPU zones need a context object made on the thread that owns the GL
		// context, before the first zone. Everything is drawn through this
		// one context, the Editor's UI included.
		VOX_PROFILE_GPU_CONTEXT();
		VOX_PROFILE_GPU_CONTEXT_NAME("Main window");

		texture::Texture2D::logCompressionSupport();

		samplers_ = std::make_unique<texture::SamplerCache>();
		textures_ = std::make_unique<texture::TextureManager>(resources);

		renderer_ = std::make_unique<renderer::Renderer>(window_);
		renderer_->setTextureSources(*textures_, samplers_->get(texture::SamplerDesc::trilinearRepeat()));
		modelManager_ = std::make_unique<graphics::model::ModelManager>();

		shaderProgram_ = LoadShaders(
			"Engine/Source/graphics/shaders/ColorVertexShader2.vert",
			"Engine/Source/graphics/shaders/ColorFragmentShader2.frag");
		renderer_->setShaderProgram(shaderProgram_);
		modelManager_->buildCubeModel();
        modelManager_->buildQuadModel();
        textRenderer_ = std::make_unique<TextRenderer>(resources);


		// Skybox: 6 faces in GL order (+X, -X, +Y, -Y, +Z, -Z).
		// Replace these paths with your actual skybox face textures.
		skybox_ = std::make_unique<skybox::Skybox>();
		const std::string skyFaces[6] = {
			"textures/night_negx.png",   // +X (right)
			"textures/night_posx.png",   // -X (left)
			"textures/night_posy.png",   // +Y (top)
			"textures/night_negy.png",   // -Y (bottom)
			"textures/night_posz.png",   // +Z (front)
			"textures/night_negz.png",   // -Z (back)
		};
		try
		{
			skybox_->init(resources, skyFaces);
		}
		catch (const std::exception& e) {
			std::cerr << "[graphics] skybox disabled: " << e.what() << '\n';
			skybox_.reset();
		}

		if (skybox_)
		{
			ibl_ = std::make_unique<ibl::IBL>();
			try
			{
				ibl_->init(skybox_->cubemapId());
			}
			catch (const std::exception& e)
			{
				std::cerr << "[graphics] IBL disabled: " << e.what() << '\n';
				ibl_.reset();
			}
		}


		// Seed the aspect ratio from the real viewport rather than a hardcoded
		// guess.
		syncAspectToViewport();

		camera_.setPerspective(glm::radians(60.f), camera_.aspect(), 0.1f, 1000.f);
		camera_.lookAt(glm::vec3(10.f), glm::vec3(0.f));

		// Park the orbit rig on the origin so switching to third person does
		// not ease the camera in from wherever it happened to be.
		orbit_.setTarget(glm::vec3(0.f));
		orbit_.snapToTarget();
	}

	/*!
		@brief
			Sets the camera mode by name.
		@param mode
			"fly" or "orbit"; anything else is ignored
		*/
    void System::setScene2D(bool enabled)
    {
        if (scene2D_ == enabled) return;
        if (enabled) {
            saved3DCamera_ = camera_;
            if (!has2DCamera_) {
                saved2DCamera_.setOrthographic(10.f, camera_.aspect(), .1f, 2000.f);
                saved2DCamera_.lookAt(glm::vec3(0, 0, 1000), glm::vec3(0));
                has2DCamera_ = true;
            }
            camera_ = saved2DCamera_;
        } else {
            saved2DCamera_ = camera_;
            camera_ = saved3DCamera_;
        }
        scene2D_ = enabled;
        cameraInput_ = {};
        syncAspectToViewport();
    }

	void System::setCameraMode(const std::string& mode)
	{
		if (mode == "fly")
			mode_ = CameraMode::Fly;
		else if (mode == "orbit")
			mode_ = CameraMode::Orbit;
	}

	/*!
		@brief
			Reads the bound GL viewport, records its size and sets the camera's aspect ratio;
			does nothing for an empty viewport.
		*/
	void System::syncAspectToViewport()
	{
		GLint viewport[4]{};
		glGetIntegerv(GL_VIEWPORT, viewport);

		// Minimized, or no window yet
		if (viewport[2] <= 0 || viewport[3] <= 0)
			return;

		viewportWidth_ = viewport[2];
		viewportHeight_ = viewport[3];
		camera_.setAspect(static_cast<float>(viewport[2]) / static_cast<float>(viewport[3]));
	}

	/*!
		@brief
			Moves the camera with the active controller from this frame's intent.
		@param dt
			Real frame time in seconds
		*/
	void System::update(double dt)
	{
		// Intent arrives already resolved from whatever device produced it.
		// Cursor capture is the input system's business, not this one's - it
		// is a main-thread-only GLFW call and it has to stay mouse-gated.
		VOX_PROFILE_SCOPE_C("Camera update", profile::colour::kGraphics);

		const camera::ControllerInput& in = cameraInput_;
		const float dtf = static_cast<float>(dt);

        if (scene2D_) {
            const float halfHeight = camera_.orthoHeight();
            const float zoom = in.scrollDelta_ + in.zoomRate_ * dtf * 8.f;
            camera_.setOrthoHeight(glm::clamp(halfHeight * std::pow(1.15f, -zoom), .05f, 10000.f));
            auto position = camera_.position();
            position.x += in.moveAxis_.x * halfHeight * dtf;
            position.y += (in.moveAxis_.y + in.moveAxis_.z) * halfHeight * dtf;
            if (in.lookActive_ && viewportHeight_ > 0) {
                const float unitsPerPixel = 2.f * halfHeight / static_cast<float>(viewportHeight_);
                position.x -= in.lookDelta_.x * unitsPerPixel;
                position.y += in.lookDelta_.y * unitsPerPixel;
            }
            camera_.setPosition(position);
            camera_.setYawPitch(0.f, 0.f);
            return;
        }
        switch (mode_)
		{
		case CameraMode::Orbit:
			orbit_.update(camera_, in, dtf);
			break;

		case CameraMode::Fly:
		default:
			fly_.update(camera_, in, dtf);
			break;
		}
	}

	/*!
		@brief
			Clears and draws the entity pass then the world pass into the bound framebuffer,
			collecting unused textures every kTextureCollectInterval frames. Does not swap.
		*/
	void System::render()
	{
		VOX_PROFILE_FUNCTION_C(profile::colour::kGraphics);
		VOX_PROFILE_GPU_SCOPE_C("Scene", profile::colour::kGraphics);

		// The caller owns the viewport: the window, or the Editor's viewport
		// framebuffer. Match the camera to whichever one is bound.
		syncAspectToViewport();

		renderer_->setClearColor(0.2f, 0.2f, 0.2f, 1.0f);
		renderer_->clear();

		if (!scene2D_) {
			// Skybox and environment lighting belong to the 3D pass only.
			if (skybox_)
				skybox_->render(camera_);
			if (ibl_)
				ibl_->bind();

			VOX_PROFILE_SCOPE("Entity pass");
			VOX_PROFILE_GPU_SCOPE("Entity pass");

			renderer_->beginFrame(camera_, !scene2D_);

			// Every entity draws as the cube until models are components.
			if (modelManager_->hasModel("cube"))
				renderer_->renderEntities(ecs_, modelManager_->getModel("cube"));

			renderer_->endFrame();
		}

		// The world (voxel terrain) draws after entities, before debug lines.
		if (worldPass_)
		{
			VOX_PROFILE_SCOPE("World pass");
			VOX_PROFILE_GPU_SCOPE("World pass");

			worldPass_->render(camera_, viewportWidth_, viewportHeight_);
		}

        if (scene2D_) {
            renderer_->beginFrame(camera_, false, true);
            renderer_->renderEntities(ecs_, modelManager_->getModel("menuQuad"));
            renderer_->endFrame();
        }

		textRenderer_->render(ecs_.registry(), camera_, viewportWidth_, viewportHeight_);

		// GL deletes belong on the render path, not in update(), which must
		// stay GL-free to remain schedulable.
		if (++framesSinceTextureCollect_ >= kTextureCollectInterval)
			collectUnusedTextures();

		// No swap here: the engine presents once, after graphics and the
		// optional editor UI have both drawn.
	}

	/*!
		@brief
			Frees textures no RenderMaterial references (hidden entities still count as in use).
		@return
			Number of textures freed
		*/
	std::size_t System::collectUnusedTextures()
	{
		framesSinceTextureCollect_ = 0;

		VOX_PROFILE_FUNCTION_C(profile::colour::kGraphics);

		if (!textures_)
			return 0;

		// Hidden entities count as in use: toggling visibility must not
		// reload a texture from disk.
		texturesInUse_.clear();

		for (auto [entity, material] : ecs_.registry().view<cpn::RenderMaterial>().each())
		{
			if (!material.albedo_.empty())
				texturesInUse_.push_back(textures_->acquire(material.albedo_, texture::ColorSpace::Srgb));
		}

		return textures_->collectUnused(texturesInUse_);
	}

	/*!
		@brief
			Name of this system for the scheduler.
		@return
			"Graphics"
		*/
	const char* System::getName() const
	{
		return "Graphics";
	}
}
