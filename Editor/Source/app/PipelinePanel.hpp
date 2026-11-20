/*************************************************************************************************************
\file       PipelinePanel.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Declares the Editor's Generation Pipeline panel: the terrain prepass as a strip of cards, one
            per pass, and a large view of the selected pass with its figures and recipe settings.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Editor
#include "terrain/PipelinePreview.hpp"

// GLM
#include <glm/glm.hpp>

// stds
#include <functional>
#include <memory>
#include <string>
#include <vector>


namespace graphics::texture
{
	class Texture2D;
}

namespace vox::editor
{
	/**
	 * The Generation Pipeline panel: the terrain prepass laid out as a strip
	 * of cards, one per pass in the order they run, each with a picture of
	 * the terrain as that pass left it, how long it took and what it did.
	 * Clicking a card opens it large, with a choice of what to look at - the
	 * shaded ground, its height, what the pass changed, water, biomes - and
	 * the part of the recipe that drives the pass.
	 *
	 * The pictures come from voxel::preview, the same renderer VoxelProbe
	 * writes to disk, and the data from PipelinePreview. Like the other
	 * panels it draws only its body and reports what the user asked for
	 * through Actions.
	 *
	 */
	class PipelinePanel
	{
	public:

		/** Where the panel sends what the user asked for. */
		struct Actions
		{
			std::function<void()>	openRecipeFile;	// in whatever the system opens .voxgen files with
			std::function<void()>	regenerate;			// rebuild the 3D terrain from the recipe on disk
			std::function<void()>	dismissReload;		// keep the terrain as it is
		};

		struct Context
		{
			const PipelinePreview::Result*	result			{};	// null before the first preview
			bool							resultChanged	{};	// this frame; the textures are rebuilt
			bool							busy			{};
			float							progress		{};
			const voxel::MaterialTable*	materials		{};
			std::string						source;				// what is being previewed, in words
			std::string						recipeFile;			// asset-relative; empty when the recipe is not a file
			// A recipe file changed on disk while the terrain holds hand edits: the preview shows
			// the new recipe, the terrain is still the old one.
			bool							reloadPending	{};
		};

		/*!
			@brief
				Creates an empty panel with no textures.

				Defined out of line so Texture2D can stay forward-declared here.
			*/
		PipelinePanel();

		/*!
			@brief
				Destroys the panel and whatever textures it still holds.

				Defined out of line so Texture2D can stay forward-declared here.
			*/
		~PipelinePanel();

		/*!
			@brief
				Draws the panel body; call between ImGui::Begin and End, on the GL thread.
			@param context
				The preview result and state to show
			@param actions
				Where the panel sends what the user asked for
			*/
		void draw(const Context& context, const Actions& actions);

		/*!
			@brief
				Frees the GL textures; call while the context is still current.
			*/
		void release();

	private:

		using Mode = voxel::preview::Mode;

		/*!
			@brief
				Renders and uploads one card picture per stage in the current card mode, with one
				height scale shared by every stage.
			@param context
				The preview result and material table to render from
			*/
		void rebuildCards(const Context& context);

		/*!
			@brief
				Renders and uploads the large picture of the selected stage in the large view's mode.
			@param context
				The preview result and material table to render from
			*/
		void rebuildLarge(const Context& context);

		/*!
			@brief
				Draws the card-mode choice and the scrolling strip of stage cards; a click selects a stage.
			@param context
				The preview result and material table
			*/
		void drawCards(const Context& context);

		/*!
			@brief
				Draws the selected stage large, with zoom, pan and a hover readout, beside its info,
				view choice, figures and recipe block.
			@param context
				The preview result and material table
			*/
		void drawLarge(const Context& context);

		/*!
			@brief
				Draws the part of the recipe a pass reads: what to change in the .voxgen to change that card.
			@param recipe
				The recipe that was previewed
			@param stage
				The pass whose settings to show
			*/
		void drawRecipeBlock(const voxel::Recipe& recipe, voxel::GenStage stage) const;

		/*!
			@brief
				Uploads a preview image as a GL texture for ImGui.
			@param image
				The RGBA preview image
			@param label
				Debug label for the texture
			@return
				The texture, or null when the image is empty or the upload failed
			*/
		std::unique_ptr<graphics::texture::Texture2D> upload(
			const voxel::preview::Image& image,
			const char* label) const;

		std::vector<std::unique_ptr<graphics::texture::Texture2D>>	cards_;
		std::unique_ptr<graphics::texture::Texture2D>					large_;

		int		selected_	{ -1 };				// stage shown large; -1 follows the last one
		Mode	cardMode_	{ Mode::Shaded };
		Mode	largeMode_	{ Mode::Shaded };
		bool	largeStale_	{ true };

		float	lowest_{ 0.f }, highest_{ 1.f };	// one height scale for every stage

		// Large view: zoom about the point under the pointer, pan by dragging.
		float		zoom_	{ 1.f };
		glm::vec2	centre_	{ 0.5f };
	};
}
