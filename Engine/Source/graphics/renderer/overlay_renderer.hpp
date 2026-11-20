/*************************************************************************************************************
\file       overlay_renderer.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      OverlayRenderer, immediate-mode overlay geometry (editor gizmos, selection volumes, debug
            shapes) queued during the frame in Scene / OnTop layers and flushed over the finished scene.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Renderer
#include "renderer.hpp"

// GLEW
#include <GL/glew.h>

// GLM
#include <glm/glm.hpp>

// stds
#include <memory>
#include <vector>


namespace graphics::camera { class Camera; }

namespace graphics::renderer
{
	/**
	 * Immediate-mode overlay geometry drawn over a finished scene: editor
	 * gizmos, selection volumes, debug shapes. Queue shapes during the frame,
	 * then flush() once while the scene's framebuffer is still bound.
	 *
	 * Two layers. Scene shapes are translucent and hidden by whatever scene
	 * geometry stands in front of them, so a volume reads as being in the
	 * world. OnTop shapes are never hidden by the scene but still sort among
	 * themselves, which is what a handle the user must always be able to
	 * grab needs.
	 *
	 * Everything is triangles; there are no GL lines, because a core profile
	 * context draws them one pixel wide. edge() makes a camera-facing strip.
	 *
	 * Colours are display-referred sRGB with straight alpha, the same
	 * convention as the debug lines: the scene shader has already encoded
	 * the framebuffer, so nothing is converted here.
	 *
	 * GL thread only. flush() puts back every piece of GL state it touches:
	 * the engine sets its state once at start-up and relies on it.
	 *
	 */
	class OverlayRenderer
	{
	public:

		enum class Layer { Scene, OnTop };

		/*!
			@brief
				Builds the program from Overlay.vert / Overlay.frag. Needs a current GL context.
			*/
		OverlayRenderer();

		/*!
			@brief
				Deletes the shader program.
			*/
		~OverlayRenderer();

		OverlayRenderer(const OverlayRenderer&)				= delete;
		OverlayRenderer& operator=(const OverlayRenderer&)	= delete;

		/*!
			@brief
				Whether the shaders built.
			@return
				False when the shaders did not build; every call is then a no-op
			*/
		bool valid() const { return program_ != 0; }

		// ------------------------- Shapes -------------------------
		//
		// All in world space. Solids are lit a little from the camera so
		// they read as round; box() and edge() are flat.

		/*!
			@brief
				Axis-aligned box, all six faces.
			@param layer
				Scene or OnTop
			@param mn
				Minimum corner
			@param mx
				Maximum corner
			@param colour
				sRGB colour, straight alpha
			*/
		void box(Layer layer, const glm::vec3& mn, const glm::vec3& mx, const glm::vec4& colour);

		/*!
			@brief
				Shaded sphere.
			@param layer
				Scene or OnTop
			@param centre
				Sphere centre
			@param radius
				Sphere radius
			@param colour
				sRGB colour, straight alpha
			*/
		void sphere(Layer layer, const glm::vec3& centre, float radius, const glm::vec4& colour);

		/*!
			@brief
				Closed cone from the centre of its base to its tip.
			@param layer
				Scene or OnTop
			@param base
				Centre of the base
			@param tip
				Apex
			@param radius
				Base radius
			@param colour
				sRGB colour, straight alpha
			*/
		void cone(Layer layer, const glm::vec3& base, const glm::vec3& tip, float radius, const glm::vec4& colour);

		/*!
			@brief
				Capped cylinder between two points.
			@param layer
				Scene or OnTop
			@param from
				Centre of one end
			@param to
				Centre of the other end
			@param radius
				Cylinder radius
			@param colour
				sRGB colour, straight alpha
			*/
		void cylinder(Layer layer, const glm::vec3& from, const glm::vec3& to, float radius, const glm::vec4& colour);

		/*!
			@brief
				A strip between two points, turned to face the eye: a line with thickness.

				The width is given per end, so a caller that wants a constant
				width on screen can make the far end wider.
			@param layer
				Scene or OnTop
			@param from
				Start point
			@param to
				End point
			@param widthFrom
				World width at from
			@param widthTo
				World width at to
			@param eye
				Camera position (in local space while a transform is set)
			@param colour
				sRGB colour, straight alpha
			*/
		void edge(Layer layer, const glm::vec3& from, const glm::vec3& to, float widthFrom, float widthTo,
				  const glm::vec3& eye, const glm::vec4& colour);

		/*!
			@brief
				The twelve edges of an axis-aligned box.
			@param layer
				Scene or OnTop
			@param mn
				Minimum corner
			@param mx
				Maximum corner
			@param width
				World width of each edge
			@param eye
				Camera position the strips face
			@param colour
				sRGB colour, straight alpha
			*/
		void boxEdges(Layer layer, const glm::vec3& mn, const glm::vec3& mx, float width, const glm::vec3& eye, const glm::vec4& colour);

		/*!
			@brief
				A flat filled circle, visible from both sides.
			@param layer
				Scene or OnTop
			@param centre
				Disc centre
			@param normal
				Disc normal
			@param radius
				Disc radius
			@param colour
				sRGB colour, straight alpha
			*/
		void disc(Layer layer, const glm::vec3& centre, const glm::vec3& normal, float radius, const glm::vec4& colour);

		/*!
			@brief
				The outline of a circle, as edge() strips of one width.
			@param layer
				Scene or OnTop
			@param centre
				Circle centre
			@param normal
				Circle normal
			@param radius
				Circle radius
			@param width
				World width of the outline
			@param eye
				Camera position the strips face
			@param colour
				sRGB colour, straight alpha
			*/
		void circle(Layer layer, const glm::vec3& centre, const glm::vec3& normal, float radius, float width,
					const glm::vec3& eye, const glm::vec4& colour);

		/*!
			@brief
				Square grid lines on a plane, out to halfExtent from the centre on
				both of the plane's axes.

				The lines fall on multiples of spacing measured from the centre.
			@param layer
				Scene or OnTop
			@param centre
				Grid centre
			@param normal
				Plane normal
			@param halfExtent
				Reach of the grid from the centre along each plane axis
			@param spacing
				Distance between lines
			@param width
				World width of each line
			@param eye
				Camera position the strips face
			@param colour
				sRGB colour, straight alpha
			*/
		void grid(Layer layer, const glm::vec3& centre, const glm::vec3& normal, float halfExtent, float spacing, float width,
				  const glm::vec3& eye, const glm::vec4& colour);

		/*!
			@brief
				A rigid transform applied to every shape queued from now on.

				Lets a caller describe an oriented box in the box's own axes. The eye
				position edge() takes must then be given in that local space too.
				resetTransform() goes back to world space; flush() does it as well.
			@param localToWorld
				Transform from local shape space to world space
			*/
		void setTransform(const glm::mat4& localToWorld);

		/*!
			@brief
				Goes back to world-space shapes.
			*/
		void resetTransform();

		/*!
			@brief
				Draws everything queued since the last flush into the bound
				framebuffer, then empties the queues.
			@param camera
				Camera to draw with
			*/
		void flush(const camera::Camera& camera);

		/*!
			@brief
				Drops what is queued without drawing it.
			*/
		void clear();

	private:

		struct Vertex
		{
			glm::vec3	pos_;
			glm::vec3	normal_;	// zero for flat shapes
			glm::vec4	colour_;
		};

		/*!
			@brief
				The vertex queue for a layer.
			@param layer
				Scene or OnTop
			@return
				That layer's queue
			*/
		std::vector<Vertex>& queue(Layer layer) { return layer == Layer::Scene ? scene_ : onTop_; }

		/*!
			@brief
				Appends one triangle to a queue, applying the current transform.
			@param out
				Queue to append to
			@param a
				First corner
			@param b
				Second corner
			@param c
				Third corner
			@param na
				Normal at a
			@param nb
				Normal at b
			@param nc
				Normal at c
			@param colour
				sRGB colour, straight alpha
			*/
		void triangle(std::vector<Vertex>& out, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
					  const glm::vec3& na, const glm::vec3& nb, const glm::vec3& nc, const glm::vec4& colour);

		/*!
			@brief
				Uploads and draws one queue.
			@param vertices
				Triangle vertices to draw
			*/
		void draw(const std::vector<Vertex>& vertices);

		GLuint					program_		{ 0 };
		GLint					locViewProj_	{ -1 };
		GLint					locToEye_		{ -1 };
		std::unique_ptr<Buffer>	buffer_;

		std::vector<Vertex>		scene_, onTop_;

		glm::mat4				transform_		{ 1.f };
		bool					transformed_	{ false };
	};
}
