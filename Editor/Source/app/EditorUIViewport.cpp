/*************************************************************************************************************
\file       EditorUIViewport.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      EditorUI viewport: renders the scene into an offscreen framebuffer shown in the Viewport panel,
            builds the mouse ray and ViewportInput, and drives the terrain region gizmo, the terrain tools
            and camera framing of the region.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "EditorUI.hpp"

// Editor
#include "EditorUICommon.hpp"

// Graphics
#include "graphics/renderer/overlay_renderer.hpp"
#include "graphics/sys/gph_sys.hpp"

// Voxel
#include "voxel/sys/voxel_sys.hpp"

// GLEW
#include <GL/glew.h>

// ImGui
#include <imgui.h>

// stds
#include <algorithm>
#include <cmath>
#include <string>
#include <utility>


namespace vox::editor
{
	/*!
		@brief
			Draws the Viewport panel: (re)creates the offscreen framebuffer at the panel's size, renders
			the scene and the editor overlays into it, restores GL state and shows the image.
		@param renderScene
			Draws the scene into whatever framebuffer is bound
		*/
	void EditorUI::viewport(const std::function<engine::SceneView()>& renderScene)
	{
		viewportHovered_ = false;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) { gizmoDragging_ = false; gizmoRecorded_ = false; }
		if (!showViewport_)
			return;

		if (ImGui::Begin("Viewport", &showViewport_))
		{
            bool mode2D = scene_->is2D();
            if (mode2D) editObjects_ = true;
            // Collapsing only hides the settings. Manipulation and shortcuts still
            // run below on the viewport image, independent of this toolbar.
            if (ImGui::BeginTable("##ViewportToolbar", 2, ImGuiTableFlags_SizingStretchProp)) {
                const float modeWidth = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x +
                    ImGui::CalcTextSize("2D scene").x;
                ImGui::TableSetupColumn("Tools", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Mode", ImGuiTableColumnFlags_WidthFixed, modeWidth);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (ImGui::ArrowButton("##GizmoSettings", showGizmoControls_ ? ImGuiDir_Down : ImGuiDir_Right))
                    showGizmoControls_ = !showGizmoControls_;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s gizmo settings. The gizmo stays usable when collapsed.",
                    showGizmoControls_ ? "Hide" : "Show");
                ImGui::SameLine();
                ImGui::BeginDisabled(mode2D || gizmoDragging_ || tools_.busy());
                ImGui::Checkbox("Object gizmo", &editObjects_);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("On: edit scene objects. Off: terrain region and brush tools. Use 1 / 2 / 3 to change gizmo tools.");
                ImGui::EndDisabled();
                ImGui::TableSetColumnIndex(1);
                ImGui::BeginDisabled(!scene_->loaded() || playing_ || pendingPlay_ || gizmoDragging_ || tools_.busy());
                if (ImGui::Checkbox("2D scene", &mode2D)) attempt([&] {
                    const auto before = scene_->snapshot();
                    scene_->set2D(mode2D);
                    pushUndo({before, 0});
                    redo_.clear();
                });
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Front-facing orthographic view. Right-drag to pan; wheel to zoom. Terrain and objects are preserved. Save the scene to keep this mode.");
                ImGui::EndDisabled();
                ImGui::EndTable();
            }
            if (mode2D) editObjects_ = true;
            if (editObjects_ && showGizmoControls_) gizmoToolbar();
			viewportHovered_ = ImGui::IsWindowHovered();
			const auto size = ImGui::GetContentRegionAvail();
			const ImVec2 imageAt = ImGui::GetCursorScreenPos();
			if (size.x >= 1 && size.y >= 1)
			{
				const auto scale = ImGui::GetIO().DisplayFramebufferScale;
				const int width = std::max(1, static_cast<int>(size.x * scale.x));
				const int height = std::max(1, static_cast<int>(size.y * scale.y));

				// ------------------------------ GL state -------------------------------

				GLint previousFramebuffer{}, previousViewport[4]{}, previousTexture{}, previousDepth{};
				glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
				glGetIntegerv(GL_VIEWPORT, previousViewport);
				glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
				glGetIntegerv(GL_RENDERBUFFER_BINDING, &previousDepth);

				// ----------------------------- Framebuffer ------------------------------

				if (!viewportFramebuffer_)
				{
					glGenFramebuffers(1, &viewportFramebuffer_);
					glGenTextures(1, &viewportTexture_);
					glGenRenderbuffers(1, &viewportDepth_);
				}

				glBindFramebuffer(GL_FRAMEBUFFER, viewportFramebuffer_);
				if (width != viewportWidth_ || height != viewportHeight_)
				{
					glBindTexture(GL_TEXTURE_2D, viewportTexture_);
					glTexImage2D(
						GL_TEXTURE_2D,
						0,
						GL_RGBA8,
						width,
						height,
						0,
						GL_RGBA,
						GL_UNSIGNED_BYTE,
						nullptr);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
					glFramebufferTexture2D(
						GL_FRAMEBUFFER,
						GL_COLOR_ATTACHMENT0,
						GL_TEXTURE_2D,
						viewportTexture_,
						0);
					glBindRenderbuffer(GL_RENDERBUFFER, viewportDepth_);
					glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
					glFramebufferRenderbuffer(
						GL_FRAMEBUFFER,
						GL_DEPTH_STENCIL_ATTACHMENT,
						GL_RENDERBUFFER,
						viewportDepth_);
					viewportWidth_ = width;
					viewportHeight_ = height;
				}
				const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
				glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
				glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(previousDepth));

				// -------------------------------- Draw ----------------------------------

				engine::SceneView camera;
				if (complete)
				{
					glViewport(0, 0, width, height);
					camera = renderScene();
					// Over the finished scene and into the same framebuffer, so terrain hides the
					// region's box and cuts a brush where it should.
                    if (!editObjects_) viewportTools(imageAt.x, imageAt.y, size.x, size.y);
                    else { gizmo_.release(); gizmoHot_ = false; toolsActive_ = false; }
				}

				// ------------------------------- Restore --------------------------------

				glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
				glViewport(
					previousViewport[0],
					previousViewport[1],
					previousViewport[2],
					previousViewport[3]);

				// ------------------------------- Present --------------------------------

				if (complete)
				{
					ImGui::Image(
						static_cast<ImTextureID>(viewportTexture_),
						size,
						ImVec2(0, 1),
						ImVec2(1, 0));
                    viewportHovered_ = ImGui::IsItemHovered();
                    if (editObjects_) {
                        gizmo(camera, imageAt.x, imageAt.y, size.x, size.y);
                        pickObject(camera, imageAt.x, imageAt.y, size.x, size.y);
                    }
					// ImGuizmo requires no hovered/active ImGui item to start a drag.
					// A full-image button stays hovered even on the next frame and blocks it.
					// Terrain tools read raw mouse input and still need their click surface.
					if (!editObjects_) {
						ImGui::SetCursorScreenPos(imageAt);
						ImGui::InvisibleButton("##scene", size, ImGuiButtonFlags_MouseButtonLeft);
					}
				}
				else
					ImGui::TextUnformatted("Unable to create the scene viewport.");
			}
		}

		ImGui::End();
	}

	/*!
		@brief
			Everything the editor draws over the scene and drives with the mouse there.

			Builds one ViewportInput from the pointer and camera, runs the region gizmo (Create tab) and
			the selected terrain tool (Edit tab), then flushes the overlay once into the bound scene
			framebuffer.
		@param imageX
			Screen x of the scene image's top-left corner
		@param imageY
			Screen y of the scene image's top-left corner
		@param imageWidth
			Width of the scene image on screen
		@param imageHeight
			Height of the scene image on screen
		*/
	void EditorUI::viewportTools(float imageX, float imageY, float imageWidth, float imageHeight)
	{
		if (!overlay_ || !graphics_)
			return;

		// -------------------------------- Pointer -------------------------------

		const graphics::camera::Camera& camera = graphics_->camera();
		const ImVec2 mouse = ImGui::GetMousePos();
		const ImGuiIO& io = ImGui::GetIO();
		const bool inside = mouse.x >= imageX && mouse.y >= imageY && mouse.x < imageX + imageWidth
		                    && mouse.y < imageY + imageHeight;
		const bool typing = io.WantTextInput || anyPopupOpen();

		// ----------------------------- ViewportInput -----------------------------

		ViewportInput input;
		// The image is drawn top row first, so the top of the panel is +1.
		const glm::vec2 ndc{ 2.f * (mouse.x - imageX) / imageWidth - 1.f,
			                 1.f - 2.f * (mouse.y - imageY) / imageHeight };
		input.ray = util::geom::perspectiveRay(
			ndc,
			camera.position(),
			camera.forward(),
			camera.right(),
			camera.up(),
			camera.fovY(),
			camera.aspect());
		input.eye = camera.position();
		input.fovY = camera.fovY();
		input.viewportHeight = imageHeight;
		// A look drag owns the cursor; a popup or another window over the viewport owns the click.
		input.over =
			viewportHovered_ && inside && !ImGui::IsMouseDown(ImGuiMouseButton_Right) && !anyPopupOpen();
		input.leftDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
		input.leftPressed = input.over && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
		input.ctrl = io.KeyCtrl;
		input.shift = io.KeyShift;
		input.alt = io.KeyAlt;
		input.adjust = !typing && ImGui::IsKeyDown(ImGuiKey_B);
		input.wheel = input.over ? io.MouseWheel : 0.f;
		input.enter = !typing
		              && (ImGui::IsKeyPressed(ImGuiKey_Enter, false)
		                  || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
		input.escape = !typing && ImGui::IsKeyPressed(ImGuiKey_Escape, false);
		input.time = ImGui::GetTime();

		// -------------------------- Gizmo, tools, flush --------------------------

		regionGizmo(input);
		terrainTools(input);

		// One flush for everything queued above, while the scene's framebuffer is still bound.
		overlay_->flush(camera);
	}

	/*!
		@brief
			Updates and draws the terrain region gizmo while the Terrain panel's Create tab shows and
			not in Play; releases it otherwise.
		@param view
			This frame's viewport pointer state
		*/
	void EditorUI::regionGizmo(const ViewportInput& view)
	{
		// Shown while the Terrain panel's Create tab is, and never in Play: the box is an authoring aid.
		const bool shown = showTerrain_ && terrain_.showRegion() && !playing_ && !pendingPlay_ && scene_->loaded();
		if (!shown)
		{
			gizmo_.release();
			gizmoHot_ = false;
			return;
		}

		RegionGizmo::Input input;
		input.ray = view.ray;
		input.overViewport = view.over;
		input.down = view.leftDown;
		input.pressed = view.leftPressed;
		input.fovY = view.fovY;
		input.viewportHeight = view.viewportHeight;

		RegionGizmo::Limits limits;
		const float vs = terrain_.voxelSize();
		limits.minSize = glm::vec3{ voxel::GenerateLimits::kMinHorizontal * vs,
			                        voxel::GenerateLimits::kMinVertical * vs,
			                        voxel::GenerateLimits::kMinHorizontal * vs };
		limits.maxSize = glm::vec3{ voxel::GenerateLimits::kMaxHorizontal * vs,
			                        voxel::GenerateLimits::kMaxVertical * vs,
			                        voxel::GenerateLimits::kMaxHorizontal * vs };
		limits.snap = terrain_.snapToVoxels() ? vs : 0.f;

		if (gizmo_.update(terrain_.region(), input, limits))
			terrain_.regionMoved();

		gizmoHot_ = gizmo_.hot();
		if (gizmoHot_)
			ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

		gizmo_.draw(*overlay_, terrain_.region(), input);
	}

	/*!
		@brief
			Updates and draws the selected terrain tool while the Terrain panel's Edit tab shows and the
			terrain is editable outside Play; deactivates the tools otherwise.
		@param input
			This frame's viewport pointer state
		*/
	void EditorUI::terrainTools(const ViewportInput& input)
	{
		ToolContext context = toolContext();

		// Tools work on the Edit tab of an open Terrain panel, and never in Play: play-time
		// edits are the game's, and a stroke's collision catch-up assumes nothing is simulating.
		const bool usable =
			showTerrain_ && terrain_.editing() && !playing_ && !pendingPlay_ && scene_->loaded() && voxel_->editable();
		if (!usable)
		{
			tools_.deactivate(context);
			toolsActive_ = false;
			return;
		}

		toolsActive_ = tools_.hasTool();
		tools_.update(input, context);
		tools_.drawOverlay(*overlay_, input, context);
	}

	/*!
		@brief
			Builds what the terrain tools need from the editor: the voxel system, a log sink and a
			callback that puts a finished stroke on the level's undo stack.
		@return
			The tool context
		*/
	ToolContext EditorUI::toolContext()
	{
		return ToolContext{
			*voxel_,
			[this](LogLevel level, std::string text) { log(level, std::move(text)); },
			// A finished stroke joins the level's undo stack, after whatever was done before it.
			[this](voxel::edit::History::Id id, std::string)
			{
				pushUndo({ std::nullopt, id });
			}
		};
	}

	/*!
		@brief
			Moves the editor camera (in Fly mode) to where the whole terrain region is in view, from
			above its south-east corner. Does nothing while playing.
		*/
	void EditorUI::frameRegion()
	{
		if (!graphics_ || playing_)
			return;

		// From above the south-east corner, far enough back that the box's diagonal fits the view.
		const TerrainRegion& region = terrain_.region();
		graphics::camera::Camera& camera = graphics_->camera();
		const float radius = 0.5f * glm::length(region.size_);
		const float distance = radius / std::tan(0.5f * camera.fovY()) * 1.15f;
		graphics_->setCameraMode(graphics::CameraMode::Fly);
		camera.lookAt(
			region.centre() + glm::normalize(glm::vec3{ 0.75f, 0.62f, 1.f }) * distance,
			region.centre());
	}
}
