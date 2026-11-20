/*************************************************************************************************************
\file       PipelinePanel.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Implements the Generation Pipeline panel: uploads preview pictures of each generator pass,
            draws the card strip and the zoomable large view, and lists the recipe settings per pass.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "PipelinePanel.hpp"

// Editor
#include "terrain/ToolWidgets.hpp"

// Graphics
#include "graphics/texture/texture.hpp"

// Voxel
#include "voxel/gen/hydrology.hpp"

// GLEW
#include <GL/glew.h>

// ImGui
#include <imgui.h>

// stds
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <iostream>
#include <span>


namespace vox::editor
{
	namespace
	{
		using voxel::GenStage;
		using voxel::preview::StageCapture;

		// Colours for a warning banner and an error message.
		const ImVec4 kWarningText(1.00f, 0.80f, 0.35f, 1);
		const ImVec4 kErrorText(1.00f, 0.45f, 0.40f, 1);

		// Width and height of one card in the strip.
		constexpr float kCardSide = 148.f;

		/*!
			@brief
				Looks up a stage's fixed name and description.
			@param stage
				The generator pass
			@return
				Its entry in kGenStages
			*/
		const voxel::GenStageInfo& infoOf(GenStage stage)
		{
			return voxel::kGenStages[static_cast<std::size_t>(stage)];
		}

		/*!
			@brief
				Capitalises the first letter, for a material name read off disk.
			@param name
				The name to capitalise
			@return
				The name with its first letter upper-case
			*/
		std::string titled(std::string name)
		{
			if (!name.empty())
				name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
			return name;
		}

		/*!
			@brief
				Computes the size an image of this aspect takes inside a box, keeping its aspect.
			@param width
				Image width in pixels
			@param height
				Image height in pixels
			@param box
				The space available
			@return
				The largest size of the image's aspect that fits the box
			*/
		ImVec2 fitted(int width, int height, const ImVec2& box)
		{
			const float aspect = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.f;
			return aspect >= box.x / std::max(1.f, box.y) ? ImVec2(box.x, box.x / aspect)
			                                              : ImVec2(box.y * aspect, box.y);
		}

		/*!
			@brief
				Describes what a pass did, in a line short enough to sit under its card.
			@param stage
				The captured pass
			@param materials
				Material table, for naming the main cover materials
			@return
				Height range, water share, top two materials or moved volume, depending on the pass
			*/
		std::string summary(const StageCapture& stage, const voxel::MaterialTable& materials)
		{
			char text[96];
			switch (stage.stage_)
			{
				case GenStage::Base:
					std::snprintf(text, sizeof(text), "%.1f to %.1f m", stage.lowest_, stage.highest_);
					return text;
				case GenStage::Lakes:
					std::snprintf(text, sizeof(text), "%.1f%% under water", stage.wetShare_ * 100.f);
					return text;
				case GenStage::Materials:
				{
					// The two materials that cover the most ground.
					std::vector<std::size_t> count(materials.size(), 0);
					for (const voxel::MaterialId m : stage.material_)
						if (m < count.size())
							++count[m];
					std::size_t first = 0, second = 0;
					for (std::size_t i = 1; i < count.size(); ++i)
					{
						if (count[i] > count[first] || first == 0)
						{
							second = first;
							first = i;
						}
						else if (count[i] > count[second] || second == 0)
							second = i;
					}
					const float total = static_cast<float>(std::max<std::size_t>(1, stage.material_.size()));
					if (first == 0)
						return "no cover";
					std::snprintf(
						text,
						sizeof(text),
						"%s %.0f%%",
						materials.get(static_cast<voxel::MaterialId>(first)).name_.c_str(),
						100.f * count[first] / total);
					std::string out = text;
					if (second != 0 && count[second] > 0)
					{
						std::snprintf(
							text,
							sizeof(text),
							", %s %.0f%%",
							materials.get(static_cast<voxel::MaterialId>(second)).name_.c_str(),
							100.f * count[second] / total);
						out += text;
					}
					return out;
				}
				default:
					std::snprintf(text, sizeof(text), "moved %.0f m3", stage.movedVolume_);
					return text;
			}
		}

		/*!
			@brief
				Draws a printf-style "label: value" row, the value lined up at a fixed column.
			@param name
				The label, drawn dimmed
			@param format
				printf format for the value
			@param ...
				Arguments for the format
			*/
		void row(const char* name, const char* format, ...)
		{
			char value[96];
			va_list args;
			va_start(args, format);
			std::vsnprintf(value, sizeof(value), format, args);
			va_end(args);
			ImGui::TextDisabled("%s", name);
			ImGui::SameLine(176.f);
			ImGui::TextUnformatted(value);
		}
	}

	PipelinePanel::PipelinePanel() = default;
	PipelinePanel::~PipelinePanel() = default;

	/*!
		@brief
			Frees the card and large-view GL textures; call while the context is still current.
		*/
	void PipelinePanel::release()
	{
		cards_.clear();
		large_.reset();
	}

	/*!
		@brief
			Uploads a preview image as a linear RGBA8 GL texture for ImGui, clamped, linear-minified
			and nearest-magnified.
		@param image
			The RGBA preview image
		@param label
			Debug label for the texture
		@return
			The texture, or null when the image is empty or the upload failed (the error is logged)
		*/
	std::unique_ptr<graphics::texture::Texture2D> PipelinePanel::upload(
		const voxel::preview::Image& image,
		const char* label) const
	{
		if (image.width_ <= 0 || image.height_ <= 0)
			return nullptr;
		try
		{
			// Linear: the bytes are display-ready and ImGui draws them as they are.
			auto texture = std::make_unique<graphics::texture::Texture2D>(
				image.width_,
				image.height_,
				vox::PixelFormat::RGBA8,
				std::span<const unsigned char>(image.rgba_),
				graphics::texture::ColorSpace::Linear,
				label,
				false);
			// ImGui binds no sampler object, so the texture's own filtering is what shows.
			glTextureParameteri(texture->id(), GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTextureParameteri(texture->id(), GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTextureParameteri(texture->id(), GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTextureParameteri(texture->id(), GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			return texture;
		}
		catch (const std::exception& e)
		{
			std::cerr << "[editor] pipeline preview texture: " << e.what() << "\n";
			return nullptr;
		}
	}

	/*!
		@brief
			Renders and uploads one card picture per stage in the current card mode, with one
			height scale shared by every stage.
		@param context
			The preview result and material table to render from
		*/
	void PipelinePanel::rebuildCards(const Context& context)
	{
		cards_.clear();
		if (!context.result || !context.materials)
			return;
		const std::vector<StageCapture>& stages = context.result->stages;
		if (stages.empty())
			return;

		lowest_ = stages.front().lowest_;
		highest_ = stages.front().highest_;
		for (const StageCapture& stage : stages)
		{
			lowest_ = std::min(lowest_, stage.lowest_);
			highest_ = std::max(highest_, stage.highest_);
		}

		const std::vector<glm::vec3> biomes = voxel::preview::biomeColours(context.result->recipe);
		for (std::size_t i = 0; i < stages.size(); ++i)
		{
			voxel::preview::RenderOptions options;
			options.mode_ = cardMode_; // render() falls back to Shaded where a stage has no such view
			options.lowest_ = lowest_;
			options.highest_ = highest_;
			options.biomeColours_ = biomes;
			cards_.push_back(upload(
				voxel::preview::render(
					stages[i],
					i > 0 ? &stages[i - 1] : nullptr,
					*context.materials,
					options),
				"pipeline card"));
		}
	}

	/*!
		@brief
			Renders and uploads the large picture of the selected stage (the last one when none is
			selected) in the large view's mode, and clears the stale flag.
		@param context
			The preview result and material table to render from
		*/
	void PipelinePanel::rebuildLarge(const Context& context)
	{
		large_.reset();
		largeStale_ = false;
		if (!context.result || !context.materials || context.result->stages.empty())
			return;
		const std::vector<StageCapture>& stages = context.result->stages;
		const int index = std::clamp(
			selected_ < 0 ? static_cast<int>(stages.size()) - 1 : selected_,
			0,
			static_cast<int>(stages.size()) - 1);
		const StageCapture* previous = index > 0 ? &stages[static_cast<std::size_t>(index) - 1] : nullptr;

		const std::vector<glm::vec3> biomes = voxel::preview::biomeColours(context.result->recipe);
		voxel::preview::RenderOptions options;
		options.mode_ = largeMode_;
		options.lowest_ = lowest_;
		options.highest_ = highest_;
		options.biomeColours_ = biomes;
		large_ = upload(
			voxel::preview::render(
				stages[static_cast<std::size_t>(index)],
				previous,
				*context.materials,
				options),
			"pipeline stage");
	}

	/*!
		@brief
			Draws the panel body: what is previewed, progress, the recipe-reload banner, then the
			cards and the large view, or a message when there is nothing or the recipe failed.

			Call between ImGui::Begin and End, on the GL thread.
		@param context
			The preview result and state to show
		@param actions
			Where the panel sends what the user asked for
		*/
	void PipelinePanel::draw(const Context& context, const Actions& actions)
	{
		if (context.resultChanged)
		{
			rebuildCards(context);
			largeStale_ = true;
		}

		ImGui::TextDisabled("Previewing");
		ImGui::SameLine();
		ImGui::TextUnformatted(context.source.empty() ? "nothing" : context.source.c_str());
		if (!context.recipeFile.empty())
		{
			ImGui::SameLine();
			if (ImGui::SmallButton("Open recipe file") && actions.openRecipeFile)
				actions.openRecipeFile();
			widgets::tooltip(
				"Opens the .voxgen in the system's editor for it. Save there and this view follows:\nthe "
			    "file is watched, and the terrain regenerates unless it has hand edits.");
		}
		if (context.busy)
		{
			ImGui::SameLine();
			ImGui::ProgressBar(context.progress, ImVec2(160, 0), "building preview");
		}

		if (context.reloadPending)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, kWarningText);
			ImGui::TextWrapped("The recipe changed on disk. This view shows the new recipe; the terrain in "
			                   "the viewport is still the old one, "
			                   "because it has hand edits that regenerating would discard. Saving the level "
			                   "now writes the old recipe back over the file.");
			ImGui::PopStyleColor();
			if (ImGui::Button("Regenerate (discards edits)") && actions.regenerate)
				actions.regenerate();
			ImGui::SameLine();
			if (ImGui::Button("Keep my edits") && actions.dismissReload)
				actions.dismissReload();
		}

		if (!context.result)
		{
			ImGui::Spacing();
			ImGui::TextDisabled(
				context.busy ? "The first preview is on its way."
							 : "Open a level with generated terrain, or choose biomes on the Terrain panel's "
			                   "Create tab.");
			return;
		}
		if (!context.result->error.empty())
		{
			ImGui::TextColored(kErrorText, "This recipe cannot be generated:");
			ImGui::TextWrapped("%s", context.result->error.c_str());
			return;
		}
		if (context.result->stages.empty() || !context.materials)
			return;

		drawCards(context);
		ImGui::Separator();
		if (largeStale_)
			rebuildLarge(context);
		drawLarge(context);
	}

	/*!
		@brief
			Draws the card-mode choice and the scrolling strip of stage cards; a click selects a
			stage and resets the large view's zoom.
		@param context
			The preview result and material table
		*/
	void PipelinePanel::drawCards(const Context& context)
	{
		const std::vector<StageCapture>& stages = context.result->stages;

		// ------------------------------ Controls ------------------------------

		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("Cards show");
		ImGui::SameLine();
		for (const Mode mode : { Mode::Shaded, Mode::Height, Mode::Change })
		{
			if (ImGui::RadioButton(std::string(voxel::preview::modeName(mode)).c_str(), cardMode_ == mode)
			    && cardMode_ != mode)
			{
				cardMode_ = mode;
				rebuildCards(context);
			}
			ImGui::SameLine();
		}
		ImGui::TextDisabled("   prepass %.0f ms in all", context.result->totalMs);

		// -------------------------------- Strip --------------------------------

		const float stripHeight =
			kCardSide + 3.f * ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ScrollbarSize + 12.f;
		if (ImGui::BeginChild(
				"##cards",
				ImVec2(0, stripHeight),
				ImGuiChildFlags_None,
				ImGuiWindowFlags_HorizontalScrollbar))
		{
			const int shown = std::clamp(
				selected_ < 0 ? static_cast<int>(stages.size()) - 1 : selected_,
				0,
				static_cast<int>(stages.size()) - 1);
			for (int i = 0; i < static_cast<int>(stages.size()); ++i)
			{
				const StageCapture& stage = stages[static_cast<std::size_t>(i)];
				const voxel::GenStageInfo& info = infoOf(stage.stage_);
				if (i > 0)
				{
					// The arrow from the pass before.
					ImGui::SameLine();
					ImGui::BeginGroup();
					ImGui::Dummy(ImVec2(1, kCardSide * 0.5f - ImGui::GetTextLineHeight()));
					ImGui::TextDisabled(">");
					ImGui::EndGroup();
					ImGui::SameLine();
				}
				ImGui::PushID(i);
				ImGui::BeginGroup();
				const graphics::texture::Texture2D* texture = static_cast<std::size_t>(i) < cards_.size()
				                                                  ? cards_[static_cast<std::size_t>(i)].get()
				                                                  : nullptr;
				const ImVec2 size =
					texture ? fitted(texture->width(), texture->height(), { kCardSide, kCardSide })
							: ImVec2(kCardSide, kCardSide);
				const bool on = i == shown;
				if (on)
					ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
				const bool clicked =
					texture ? ImGui::ImageButton("##card", static_cast<ImTextureID>(texture->id()), size)
							: ImGui::Button("##card", size);
				if (on)
					ImGui::PopStyleColor();
				if (clicked)
				{
					selected_ = i;
					largeStale_ = true;
					zoom_ = 1.f;
					centre_ = glm::vec2{ 0.5f };
				}
				widgets::tooltip(std::string(info.does_).c_str());

				ImGui::Text("%d  %s", i + 1, std::string(info.name_).c_str());
				ImGui::TextDisabled("%.0f ms", stage.milliseconds_);
				ImGui::TextDisabled("%s", summary(stage, *context.materials).c_str());
				ImGui::EndGroup();
				ImGui::PopID();
			}
		}
		ImGui::EndChild();
	}

	/*!
		@brief
			Draws the selected stage large, with wheel zoom, drag pan and a hover readout, beside
			its description, view choice, figures and recipe block.
		@param context
			The preview result and material table
		*/
	void PipelinePanel::drawLarge(const Context& context)
	{
		const std::vector<StageCapture>& stages = context.result->stages;
		const int index = std::clamp(
			selected_ < 0 ? static_cast<int>(stages.size()) - 1 : selected_,
			0,
			static_cast<int>(stages.size()) - 1);
		const StageCapture& stage = stages[static_cast<std::size_t>(index)];
		const StageCapture* previous = index > 0 ? &stages[static_cast<std::size_t>(index) - 1] : nullptr;
		const voxel::GenStageInfo& info = infoOf(stage.stage_);

		const float infoWidth = std::min(330.f, ImGui::GetContentRegionAvail().x * 0.42f);
		const ImVec2 area{
			std::max(64.f, ImGui::GetContentRegionAvail().x - infoWidth - ImGui::GetStyle().ItemSpacing.x),
			std::max(64.f, ImGui::GetContentRegionAvail().y)
		};

		// ------------------------------ Picture ------------------------------
		// Wheel zooms about the pointer, a drag pans.

		ImGui::BeginChild(
			"##large",
			area,
			ImGuiChildFlags_None,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		if (large_)
		{
			const ImVec2 size = fitted(large_->width(), large_->height(), ImGui::GetContentRegionAvail());
			const ImVec2 at = ImGui::GetCursorScreenPos();
			const float span = 0.5f / zoom_;
			centre_ = glm::clamp(centre_, glm::vec2{ span }, glm::vec2{ 1.f - span });
			const glm::vec2 uv0 = centre_ - span, uv1 = centre_ + span;
			ImGui::Image(
				static_cast<ImTextureID>(large_->id()),
				size,
				ImVec2(uv0.x, uv0.y),
				ImVec2(uv1.x, uv1.y));
			ImGui::SetCursorScreenPos(at);
			ImGui::InvisibleButton("##pan", size, ImGuiButtonFlags_MouseButtonLeft);

			if (ImGui::IsItemHovered())
			{
				ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
				const ImVec2 mouse = ImGui::GetMousePos();
				const glm::vec2 within{ (mouse.x - at.x) / size.x, (mouse.y - at.y) / size.y };
				const glm::vec2 uv = uv0 + within * (uv1 - uv0);

				if (const float wheel = ImGui::GetIO().MouseWheel; wheel != 0.f)
				{
					const float next = std::clamp(zoom_ * std::pow(1.25f, wheel), 1.f, 16.f);
					// The point under the pointer stays under it.
					centre_ = uv + (centre_ - uv) * (zoom_ / next);
					zoom_ = next;
				}

				// What is under the pointer.
				const int x = std::clamp(
					static_cast<int>(uv.x * static_cast<float>(stage.height_.width_)),
					0,
					stage.height_.width_ - 1);
				const int z = std::clamp(
					static_cast<int>(uv.y * static_cast<float>(stage.height_.depth_)),
					0,
					stage.height_.depth_ - 1);
				const std::size_t i = stage.height_.index(x, z);
				const float metresX = (static_cast<float>(x) + 0.5f) * stage.height_.cell_,
							metresZ = (static_cast<float>(z) + 0.5f) * stage.height_.cell_;
				ImGui::BeginTooltip();
				ImGui::Text("x %.1f m, z %.1f m", metresX, metresZ);
				ImGui::Text("height %.2f m", stage.height_.h_[i]);
				if (stage.sediment_.size() != 0)
					ImGui::Text(
						"%s %.2f m so far",
						stage.sediment_.h_[i] >= 0.f ? "gained" : "lost",
						std::fabs(stage.sediment_.h_[i]));
				if (stage.water_.size() != 0 && stage.water_.h_[i] > voxel::kNoWater * 0.5f
				    && stage.water_.h_[i] > stage.height_.h_[i])
					ImGui::Text("water %.2f m deep", stage.water_.h_[i] - stage.height_.h_[i]);
				if (!stage.material_.empty() && stage.material_[i] < context.materials->size())
					ImGui::Text("%s", titled(context.materials->get(stage.material_[i]).name_).c_str());
				if (!stage.biome_.empty() && stage.biome_[i] < context.result->recipe.biomes_.entries_.size())
					ImGui::Text(
						"biome: %s",
						context.result->recipe.biomes_.entries_[stage.biome_[i]].name_.c_str());
				ImGui::EndTooltip();
			}
			if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.f))
			{
				const ImVec2 delta = ImGui::GetIO().MouseDelta;
				centre_ -= glm::vec2{ delta.x / size.x, delta.y / size.y } / zoom_;
			}
		}
		ImGui::EndChild();

		// -------------------------------- Info --------------------------------
		// What it is, what to look at, and what drives it.

		ImGui::SameLine();
		ImGui::BeginChild("##info", ImVec2(0, area.y));
		ImGui::Text("%d  %s", index + 1, std::string(info.name_).c_str());
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		ImGui::TextWrapped("%s", std::string(info.does_).c_str());
		ImGui::PopStyleColor();
		ImGui::Spacing();

		ImGui::TextDisabled("View");
		for (int m = 0; m < static_cast<int>(Mode::Count); ++m)
		{
			const Mode mode = static_cast<Mode>(m);
			const bool available = voxel::preview::supports(stage, previous, mode);
			ImGui::BeginDisabled(!available);
			if (ImGui::RadioButton(
					std::string(voxel::preview::modeName(mode)).c_str(),
					largeMode_ == mode && available)
			    && largeMode_ != mode)
			{
				largeMode_ = mode;
				largeStale_ = true;
			}
			ImGui::EndDisabled();
			if (m % 3 != 2 && m + 1 < static_cast<int>(Mode::Count))
				ImGui::SameLine();
		}
		if (largeMode_ == Mode::Change)
			ImGui::TextDisabled("red: lowered   blue: raised");
		if (zoom_ > 1.f)
		{
			if (ImGui::SmallButton("Reset zoom"))
			{
				zoom_ = 1.f;
				centre_ = glm::vec2{ 0.5f };
			}
		}
		else
			ImGui::TextDisabled("wheel zooms, drag pans");

		ImGui::Spacing();
		ImGui::Separator();
		row("Time", "%.0f ms", stage.milliseconds_);
		row("Height", "%.2f to %.2f m", stage.lowest_, stage.highest_);
		if (previous)
			row("Ground moved", "%.0f m3", stage.movedVolume_);
		if (stage.water_.size() != 0)
			row("Under water", "%.1f%%", stage.wetShare_ * 100.f);
		row("Grid", "%d x %d at %.2f m", stage.height_.width_, stage.height_.depth_, stage.height_.cell_);

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::TextDisabled("Recipe");
		drawRecipeBlock(context.result->recipe, stage.stage_);
		ImGui::EndChild();
	}

	/*!
		@brief
			Draws the recipe settings a pass reads, as label / value rows.
		@param r
			The recipe that was previewed
		@param stage
			The pass whose settings to show
		*/
	void PipelinePanel::drawRecipeBlock(const voxel::Recipe& r, GenStage stage) const
	{
		switch (stage)
		{
			case GenStage::Base:
				row("seed", "%llu", static_cast<unsigned long long>(r.seed_));
				row("size", "%d x %d x %d voxels", r.size_.x, r.size_.y, r.size_.z);
				if (r.biomes_.enabled_)
				{
					std::string names;
					for (const auto& biome : r.biomes_.entries_)
						names += (names.empty() ? "" : ", ") + biome.name_;
					row("biomes", "%s", names.c_str());
					row("biomes.size", "%.0f m", r.biomes_.size_);
					row("biomes.blending", "%.2f", r.biomes_.blending_);
					row("biomes.reliefScale", "%.2f", r.biomes_.reliefScale_);
				}
				else
				{
					row("shape.massif",
					    "%.0f m waves, %.1f m high",
					    r.shape_.massif_.wavelength_,
					    r.shape_.massif_.amplitude_);
					row("shape.basins", "%d", static_cast<int>(r.shape_.basins_.size()));
				}
				break;
			case GenStage::Hydraulic:
				row("erosion.droplets", "%d", r.erosion_.droplets_);
				row("erosion.cellVoxels", "%d", r.erosion_.cellVoxels_);
				row("erosion.radius", "%d", r.erosion_.radius_);
				row("erosion.capacity", "%.2f", r.erosion_.capacity_);
				row("erosion.erode / deposit", "%.2f / %.2f", r.erosion_.erode_, r.erosion_.deposit_);
				row("erosion.minRelief", "%.1f m", r.erosion_.minRelief_);
				break;
			case GenStage::Thermal:
				row("thermal.iterations", "%d", r.erosion_.thermalIterations_);
				row("thermal.rockTalus", "%.0f deg", r.erosion_.rockTalusDeg_);
				row("thermal.sedimentTalus", "%.0f deg", r.erosion_.sedimentTalusDeg_);
				row("thermal.rate", "%.2f", r.erosion_.thermalRate_);
				break;
			case GenStage::Lakes:
				row("water.enabled", "%s", r.water_.enabled_ ? "yes" : "no");
				row("water.minDepth", "%.2f m", r.water_.minDepth_);
				row("water.minArea", "%.0f m2", r.water_.minArea_);
				row("water.maxDepth", "%.1f m", r.water_.maxDepth_);
				row("water.fillFraction", "%.2f", r.water_.fillFraction_);
				row("water.maxArea", "%.0f m2", r.water_.maxArea_);
				break;
			case GenStage::Detail:
				row("detail.wavelength", "%.2f m", r.detailNoise_.layer_.wavelength_);
				row("detail.amplitude", "%.2f m", r.detailNoise_.layer_.amplitude_);
				row("detail.fullSlope", "%.0f deg", r.detailNoise_.fullSlopeDeg_);
				break;
			default:
				row("surface.grassMaxSlope", "%.0f deg", r.surface_.grassMaxSlopeDeg_);
				row("surface.rockSlope", "%.0f deg", r.surface_.rockSlopeDeg_);
				row("surface.snowline", "%.1f m", r.surface_.snowline_);
				row("surface.sandMaxHeight", "%.1f m", r.surface_.sandMaxHeight_);
				row("surface.soilDepth", "%.1f m", r.surface_.soilDepth_);
				if (r.biomes_.enabled_)
					ImGui::TextDisabled("Each biome overrides these with its own.");
				break;
		}
	}
}
