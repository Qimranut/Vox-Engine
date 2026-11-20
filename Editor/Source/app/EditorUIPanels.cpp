	/*************************************************************************************************************
\file       EditorUIPanels.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      EditorUI panels: Hierarchy, Inspector (components, audio playback, apply / revert / duplicate),
            Content Browser wiring and asset preview details, Resources and the Profiler panel.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "EditorUI.hpp"

// Editor
#include "EditorUICommon.hpp"

// Engine
#include "audio/audio_events.hpp"
#include "events/bus.hpp"

// ImGui
#include <imgui.h>

// GLM
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

// stds
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>


namespace vox::editor
{
	/*!
		@brief
			Draws the Hierarchy panel: the level name, Add object / Delete, and one selectable row per
			scene object.
		*/
	void EditorUI::hierarchy()
	{
		if (!showHierarchy_)
			return;

		if (!ImGui::Begin("Hierarchy", &showHierarchy_))
		{
			ImGui::End();
			return;
		}

		ImGui::Text("%s%s", levelName().c_str(), levelDirty() ? " *" : "");

		ImGui::BeginDisabled(!scene_->loaded());
		if (ImGui::Button("Add object"))
			attempt(
				[&]
				{
					remember();
					select(scene_->create());
				});

		ImGui::SameLine();
		ImGui::BeginDisabled(!selected_);
		if (ImGui::Button("Delete"))
			attempt(
				[&]
				{
					remember();
					scene_->erase(selected_);
					selected_ = 0;
				});
		ImGui::EndDisabled();
		ImGui::EndDisabled();

		ImGui::Separator();
		for (const auto& o : scene_->objects())
		{
			ImGui::PushID(static_cast<int>(o.id));
			if (ImGui::Selectable(o.name.c_str(), o.id == selected_))
				select(o.id);
			ImGui::PopID();
		}

		ImGui::End();
	}

	/*!
		@brief
			Selects a scene object and loads it into the inspector's working copy and text buffers.
		@param id
			Scene object ID; 0 selects nothing
		*/
	void EditorUI::select(uint32_t id)
	{
		inspectorEditId_ = 0; selected_ = id;

		for (const auto& o : scene_->snapshot().objects)
			if (o.id == id)
			{
				showAssetReferences_ = !o.model.empty() || !o.texture.empty(); draft_ = o;
				rotationDegrees_ = glm::degrees(glm::eulerAngles(o.transform.rot_));

				copy(name_, o.name);
				copy(model_, o.model);
				copy(texture_, o.texture);
				copy(audioPath_, o.audio.path);
                copy(fontPath_, o.text.font);
                copy(textContent_, o.text.content);
				break;
			}
	}

	/*!
		@brief
			Draws the Inspector panel for the selected object: name, component sections (Transform,
			Visibility, Audio Source with playback controls), asset paths, and Apply / Revert / Duplicate.
		*/
	void EditorUI::inspector()
	{
		if (!showInspector_)
			return;

		if (!ImGui::Begin("Inspector", &showInspector_))
		{
			ImGui::End();
			return;
		}

		if (selected_)
		{
			//@@ Header

			ImGui::Text("Object ID: %u", selected_);
			ImGui::InputText("Name", name_, sizeof(name_));

			if (ImGui::Button("Add component"))
				ImGui::OpenPopup("AddComponent");

			if (ImGui::BeginPopup("AddComponent"))
			{
				if (ImGui::MenuItem("Transform", nullptr, false, !draft_.hasTransform))
					attempt([&] { changeComponent(SceneComponent::Transform, true); });
				if (ImGui::MenuItem("Visibility", nullptr, false, !draft_.hasVisibility))
					attempt([&] { changeComponent(SceneComponent::Visibility, true); });
				if (ImGui::MenuItem("Audio Source", nullptr, false, !draft_.hasAudioSource))
					attempt([&] { changeComponent(SceneComponent::AudioSource, true); });

				if (ImGui::MenuItem("Text", nullptr, false, !draft_.hasText))
                    attempt([&] { changeComponent(SceneComponent::Text, true); });

                if (draft_.hasTransform && draft_.hasVisibility && draft_.hasAudioSource && draft_.hasText)
					ImGui::TextDisabled("All available components are attached.");

				ImGui::EndPopup();
			}

            // Live Transform edits commit only this component, leaving pending
            // name/asset/audio fields in the incoming Inspector untouched.
            const auto commitTransform = [&] {
                auto before = scene_->snapshot();
                const auto current = std::find_if(before.objects.begin(), before.objects.end(),
                    [&](const auto& object) { return object.id == selected_; });
                if (current == before.objects.end() || !current->hasTransform)
                    throw std::runtime_error("The selected Transform no longer exists.");
                auto object = *current;
                object.transform = draft_.transform;
                try { scene_->update(object); }
                catch (...) {
                    draft_.transform = current->transform;
                    rotationDegrees_ = glm::degrees(glm::eulerAngles(current->transform.rot_));
                    throw;
                }
                const auto editId = ImGui::GetItemID();
                if (!editId || inspectorEditId_ != editId) pushUndo({std::move(before), 0});
                inspectorEditId_ = editId;
            };

			//@@ Transform

			if (draft_.hasTransform)
			{
				ImGui::SeparatorText("Transform");

				if (ImGui::SmallButton("Remove Transform"))
					attempt([&] { changeComponent(SceneComponent::Transform, false); });
				else
				{
					if (ImGui::DragFloat3("Position", &draft_.transform.pos_.x, .05f))
                        attempt(commitTransform);

					// Transform stores a quaternion; edit Euler degrees and convert back.
					if (ImGui::DragFloat3("Rotation (degrees)", &rotationDegrees_.x, .5f)) {
                        draft_.transform.rot_ = glm::quat(glm::radians(rotationDegrees_));
                        attempt(commitTransform);
                    }

					if (ImGui::DragFloat3("Scale", &draft_.transform.scale_.x, .01f))
                        attempt(commitTransform);
                    ImGui::TextDisabled("Transform changes apply immediately. Ctrl+Z to undo.");
                    if (draft_.hasText && draft_.text.screenSpace)
                        ImGui::TextWrapped("Screen-space text uses Screen position and Font size in its Text component; Transform affects world-space objects.");
				}
			}

            if (!ImGui::IsAnyItemActive()) inspectorEditId_ = 0;

			//@@ Visibility

			if (draft_.hasVisibility)
			{
				ImGui::SeparatorText("Visibility");

				if (ImGui::SmallButton("Remove Visibility"))
					attempt([&] { changeComponent(SceneComponent::Visibility, false); });
				else
					ImGui::Checkbox("Visible", &draft_.visible);
			}

			//@@ Audio Source

			if (draft_.hasAudioSource)
			{
				ImGui::SeparatorText("Audio Source");

				if (ImGui::SmallButton("Remove Audio Source"))
					attempt([&] { changeComponent(SceneComponent::AudioSource, false); });
				else
				{
					ImGui::InputText("Sound (.wav / .mp3)", audioPath_, sizeof(audioPath_));
					ImGui::TextDisabled("Path relative to Assets, e.g. audio/music.mp3");

					if (preview_ && preview_->kind == ResourceKind::Audio
						&& ImGui::Button("Use selected audio asset"))
						copy(audioPath_, preview_->path);

					int channel = static_cast<int>(draft_.audio.channel);
					if (ImGui::Combo("Channel", &channel, "Music\0SFX\0UI\0"))
						draft_.audio.channel = static_cast<cpn::AudioChannel>(channel);

					ImGui::SliderFloat("Volume", &draft_.audio.volume, 0, 1);
					ImGui::Checkbox("Loop", &draft_.audio.loop);
					ImGui::Checkbox("Play on Awake", &draft_.audio.playOnAwake);
					ImGui::TextDisabled("Apply changes before playback. Toolbar Play enables audio.");

					audio::SourceState soundState;
					bus_->publish(audio::QuerySource{ scene_->entity(selected_), soundState });
					ImGui::Text(
						"Playback: %s | %.2f s",
						!soundState.playing ? "Stopped"
						: soundState.paused ? "Paused"
						:                     "Playing",
						soundState.positionMs / 1000.0);

					ImGui::BeginDisabled(!playing_);
					if (ImGui::Button("Play / Resume sound"))
						attempt(
							[&]
							{
								bus_->publish(
									audio::SourceCommand{ scene_->entity(selected_), audio::Action::Play });
							});

					ImGui::SameLine();
					if (ImGui::Button("Pause sound"))
						attempt(
							[&]
							{
								bus_->publish(
									audio::SourceCommand{ scene_->entity(selected_), audio::Action::Pause });
							});
					ImGui::EndDisabled();

					if (ImGui::Button("Stop sound"))
						attempt(
							[&]
							{
								bus_->publish(
									audio::SourceCommand{ scene_->entity(selected_), audio::Action::Stop });
							});
				}
			}

            if (draft_.hasText) {
                ImGui::SeparatorText("Text");
                if (ImGui::SmallButton("Remove Text")) attempt([&] { changeComponent(SceneComponent::Text, false); });
                else {
                    if (ImGui::BeginCombo("Font", fontPath_[0] ? fontPath_ : "<None>")) {
                        for (const auto& file : browser_.files()) {
                            auto ext = std::filesystem::path(file).extension().string();
                            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                            if (ext != ".ttf" && ext != ".otf") continue;
                            if (ImGui::Selectable(file.c_str(), file == fontPath_)) copy(fontPath_, file);
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::InputTextMultiline("Content", textContent_, sizeof(textContent_), ImVec2(-1, 80));
                    if (ImGui::Checkbox("Screen space (UI)", &draft_.text.screenSpace))
                        draft_.text.size = draft_.text.screenSpace ? 32.f : 1.f;
                    if (draft_.text.screenSpace) ImGui::DragFloat2("Screen position", &draft_.text.screenPosition.x, 1.f);
                    ImGui::DragFloat("Font size", &draft_.text.size, .1f, .01f, 4096.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                    ImGui::ColorEdit4("Text colour", &draft_.text.color.x);
                    ImGui::Combo("Alignment", &draft_.text.alignment, "Left\0Centre\0Right\0");
                    ImGui::TextDisabled("Use Apply changes below to update the text.");
                }
            }

			//@@ Assets

			ImGui::SeparatorText("Assets");
			ImGui::InputText("Model", model_, sizeof(model_));
			ImGui::InputText("Texture", texture_, sizeof(texture_));

			//@@ Apply / Revert / Duplicate

			if (ImGui::Button("Apply changes"))
				attempt(
					[&]
					{
						draft_.name = name_;
						draft_.model = model_;
						draft_.texture = texture_;
						draft_.audio.path = audioPath_;
                        draft_.text.font = fontPath_;
                        draft_.text.content = textContent_;

						remember();
						scene_->update(draft_);
						select(selected_);
						log(LogLevel::Info, "Updated " + draft_.name + ".");
					});

			ImGui::SameLine();
			if (ImGui::Button("Revert fields"))
				select(selected_);

			if (ImGui::Button("Duplicate"))
				attempt(
					[&]
					{
						auto d = scene_->snapshot();
						auto object = draft_;

						uint32_t id = 1;
						for (const auto& o : d.objects)
						{
							if (o.id == UINT32_MAX)
								throw std::runtime_error("Object ID limit reached.");

							id = std::max(id, o.id + 1);
						}

						object.id = id;
						object.name += " Copy";
						d.objects.push_back(object);

						remember();
						scene_->replace(d);
						select(id);
					});
		}
		else
			ImGui::TextDisabled("No selection");

		ImGui::End();
	}

	/*!
		@brief
			Adds or removes a component on the selected object through SceneManager, as one undo entry,
			and reloads the inspector.
		@param component
			Which component
		@param present
			True to add it, false to remove it
		*/
	void EditorUI::changeComponent(SceneComponent component, bool present)
	{
		// Commit through SceneManager, so component presence participates in save/load
		// and the same undo history as other inspector edits.
		auto before = scene_->snapshot();
		scene_->setComponent(selected_, component, present);
		pushUndo({ std::move(before), 0 });

		select(selected_);
	}

	/*!
		@brief
			Draws the Content Browser panel, wiring its actions to level commands, and the details of
			the previewed asset with Release / Reload / Add to scene assets.
		*/
	void EditorUI::contentPanel()
	{
		if (!showAssets_)
			return;

		if (focusContentFrames_ > 0 && --focusContentFrames_ == 0)
			ImGui::SetNextWindowFocus();

		if (!ImGui::Begin("Content Browser", &showAssets_))
		{
			ImGui::End();
			return;
		}

		//@@ Actions

		ContentBrowser::Actions actions;
		actions.openLevel = [&](const std::string& relative)
		{
			request(Command::OpenLevel, resources_->root() / relative);
		};
		actions.useTerrain = [&](const std::string& relative)
		{
			request(Command::UseTerrain, relative);
		};

		// A recipe file generates the same way the Terrain panel does: through the command, so hand
		// edits are asked about and a borrowed map is let go of first.
		actions.pickRecipe = [&](const std::string& relative)
		{
			attempt(
				[&]
				{
					const auto bytes = readFileBytes(resources_->resolve(relative));
					if (!bytes)
						throw std::runtime_error(relative + " could not be read.");

					pendingRecipe_ = voxel::Recipe::parse(*bytes);
					request(Command::GenerateTerrain);
				});
		};
		actions.preview = [&](const std::string& relative)
		{
			attempt([&] { showPreview(relative); });
		};
		actions.newLevel = [&](const std::string& folder)
		{
			newLevelThenSaveAs_ = true;
			newLevelFolder_ = folder;
			request(Command::NewLevel);
		};
		actions.report = [&](const std::string& message, bool error)
		{
			log(error ? LogLevel::Error : LogLevel::Info, message);
		};

		//@@ Browser

		const float details =
			preview_
				? ImGui::GetTextLineHeightWithSpacing() * 3 + ImGui::GetFrameHeightWithSpacing() + 8.0f
				: 0.0f;

		browser_.draw(actions, levelRelative(), details);

		//@@ Preview details

		if (preview_)
		{
			ImGui::Text("%s | %zu bytes", preview_->path.c_str(), preview_->memoryBytes());

			if (preview_->kind == ResourceKind::Texture)
			{
				ImGui::Text(
					"%s texture: %d x %d, %zu mip level(s)%s",
					pixelFormatName(preview_->format),
					preview_->width,
					preview_->height,
					preview_->mips.size(),
					preview_->srgbHint ? ", sRGB" : "");

				// CPU bytes equal what the GPU stores: BCn stays compressed in both.
				const auto mib = [](size_t bytes)
				{
					return static_cast<double>(bytes) / (1024.0 * 1024.0);
				};

				const size_t raw = uncompressedEquivalentBytes(*preview_);
				const std::string origin =
					preview_->cookedPath.empty() ? "source DDS" : "cooked: " + preview_->cookedPath;

				ImGui::Text(
					"%s | %.2f MB vs %.2f MB uncompressed (%.1f:1)",
					origin.c_str(),
					mib(preview_->bytes.size()),
					mib(raw),
					preview_->bytes.empty()
						? 0.0
						: static_cast<double>(raw) / static_cast<double>(preview_->bytes.size()));
			}
			else if (preview_->kind == ResourceKind::Model)
				ImGui::Text("Static 3D model: %zu meshes", preview_->meshes.size());
			else if (preview_->kind == ResourceKind::Font)
                ImGui::TextUnformatted("Font atlas: printable ASCII, 64px (.ttf / .otf)");
            else if (preview_->kind == ResourceKind::Audio)
				ImGui::TextUnformatted("Audio clip (.wav / .mp3)");
			else
				ImGui::TextUnformatted("Binary file");

			if (ImGui::Button("Release preview"))
			{
				preview_.reset();
				resources_->collectUnused();
			}

			ImGui::SameLine();
			if (ImGui::Button("Reload file") && preview_)
				attempt([&] { preview_ = resources_->reload(preview_->path); });

			ImGui::SameLine();
			ImGui::BeginDisabled(!scene_->loaded());
			if (ImGui::Button("Add to scene assets") && preview_)
				attempt(
					[&]
					{
						auto d = scene_->snapshot();
						if (std::find(d.assets.begin(), d.assets.end(), preview_->path) == d.assets.end())
						{
							remember();
							d.assets.push_back(preview_->path);
							scene_->replace(d);
						}
					});
			ImGui::EndDisabled();
		}

		ImGui::End();
	}

	/*!
		@brief
			Loads an asset as the previewed one, unless it already is.
		@param relative
			Asset-relative path of the file
		*/
	void EditorUI::showPreview(const std::string& relative)
	{
		if (preview_ && preview_->path == relative)
			return;

		preview_ = resources_->load(relative);
	}

	/*!
		@brief
			Draws the Resources panel: cache statistics, Collect unused, and the scene's asset list with
			a Remove button per entry.
		*/
	void EditorUI::resourcePanel()
	{
		if (!showResources_)
			return;

		if (!ImGui::Begin("Resources", &showResources_))
		{
			ImGui::End();
			return;
		}

		ImGui::Text(
			"Cached: %zu | CPU data: %zu bytes",
			resources_->entries().size(),
			resources_->memoryBytes());

		if (ImGui::Button("Collect unused resources"))
			resources_->collectUnused();

		auto d = scene_->snapshot();
		for (size_t i = 0; i < d.assets.size(); ++i)
		{
			ImGui::PushID(static_cast<int>(i));

			if (ImGui::SmallButton("Remove"))
				attempt(
					[&]
					{
						remember();
						d.assets.erase(d.assets.begin() + i);
						scene_->replace(d);
					});

			ImGui::SameLine();
			if (i < d.assets.size())
				ImGui::TextUnformatted(d.assets[i].c_str());

			ImGui::PopID();
		}

		ImGui::End();
	}

	/*!
		@brief
			Draws the Profiler panel window, focusing it when asked, and routes its messages to the Console.
		*/
	void EditorUI::profilerPanel()
	{
		if (!showProfiler_)
			return;

		if (focusProfiler_)
			ImGui::SetNextWindowFocus();
		focusProfiler_ = false;

		if (ImGui::Begin("Profiler", &showProfiler_))
		{
			ProfilerPanel::Actions actions;
			actions.info = [this](const std::string& text)
			{
				log(LogLevel::Info, text);
			};
			actions.error = [this](const std::string& text)
			{
				log(LogLevel::Error, text);
			};

			profiler_.draw(actions);
		}

		ImGui::End();
	}
}
