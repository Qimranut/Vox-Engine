/*************************************************************************************************************
\file       camera.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Yaw/pitch Camera value type with cached matrices, plus the Plane and Frustum used for culling.
            Camera conventions - everything in this file depends on them:

            Space:    right-handed, -Z forward, +Y up. This is GLM's default and the default OpenGL NDC
                      depth range [-1, 1]. Do NOT define GLM_FORCE_DEPTH_ZERO_TO_ONE anywhere; the frustum
                      extraction assumes [-1, 1].

            Rotation: yaw/pitch Euler angles, in RADIANS. Degrees appear only at the Lua/designer boundary.
                      Yaw is measured from -Z and positive yaw turns LEFT; pitch is positive looking up.

                          forward = ( -sin(yaw)cos(pitch), sin(pitch), -cos(yaw)cos(pitch) )
                          right   = (  cos(yaw),           0.f,        -sin(yaw) )
                          up      = cross(right, forward)

                      So yaw = pitch = 0 gives forward = (0,0,-1), right = (1,0,0).
                      CONSEQUENCE FOR CONTROLLERS: moving the mouse right must DECREASE yaw. Getting that
                      backwards is the classic bug and it is not obvious from the math.

                      Euler rather than a quaternion because every camera this engine needs is
                      pitch-clamped and roll-free, so gimbal lock is unreachable, while clamping,
                      serializing and exposing yaw/pitch stays trivial - and yaw IS the character's heading
                      once the orbit camera drives a player.

            Roll:     not supported. lookAt() discards any roll in the supplied up vector. That is inherent
                      to a yaw/pitch camera, not a bug.

            This class deliberately contains NO OpenGL and NO GLFW - it is a pure value type, so it can
            later be dropped into a cpn::Camera component without dragging the renderer along.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// GLM
#include <glm/glm.hpp>

// stds
#include <array>


namespace graphics::camera
{
	// Which projection recomputeProj() builds.
	enum class Projection
	{
		Perspective,
		Orthographic
	};

	/**
	 * A plane in the form dot(n_, p) + d_ = 0, with n_ normalized so the
	 * expression yields a true signed distance.
	 */
	struct Plane
	{
		glm::vec3	n_{ 0.f, 1.f, 0.f };
		float		d_{ 0.f };

		/*!
			@brief
				Signed distance from the plane to a point.
			@param p
				Point to measure
			@return
				Positive in front of the plane, negative behind
			*/
		float signedDistance(const glm::vec3& p) const
		{
			return glm::dot(n_, p) + d_;
		}
	};

	/**
	 * The six clipping planes of the view volume, all facing inwards.
	 */
	struct Frustum
	{
		enum Side { Left = 0, Right, Bottom, Top, Near, Far, Count };

		std::array<Plane, Count> planes_{};
	};


	class Camera
	{
	public:

		// Transform

		/*!
			@brief
				Moves the camera to a world position.
			@param position
				New world position of the eye
			*/
		void setPosition(const glm::vec3& position);

		/*!
			@brief
				Sets the camera orientation.
			@param yawRad
				Yaw in radians, measured from -Z, positive turning left
			@param pitchRad
				Pitch in radians, positive looking up. NOT clamped here - clamping
				is controller policy, see FlyController / OrbitController.
			*/
		void setYawPitch(float yawRad, float pitchRad);

		/*!
			@brief
				Places the camera and aims it at a target, decomposing the
				direction back into yaw/pitch.
			@param eye
				World position of the eye
			@param target
				World position to look at
			@param up
				Ignored except to disambiguate; roll is always discarded
			*/
		void lookAt(const glm::vec3& eye,
					const glm::vec3& target,
					const glm::vec3& up = { 0.f, 1.f, 0.f });

		// Projection

		/*!
			@brief
				Switches to a perspective projection and sets all of its parameters.
			@param fovYRad
				Vertical field of view, in radians
			@param aspect
				Width / height. Ignored if not strictly positive.
			@param nearZ
				Near clip distance
			@param farZ
				Far clip distance
			*/
		void setPerspective(float fovYRad, float aspect, float nearZ, float farZ);

		/*!
			@brief
				Switches to an orthographic projection and sets all of its parameters.
			@param halfHeight
				Half the vertical extent of the view volume, in world units
			@param aspect
				Width / height. Ignored if not strictly positive.
			@param nearZ
				Near clip distance
			@param farZ
				Far clip distance
			*/
		void setOrthographic(float halfHeight, float aspect, float nearZ, float farZ);

		/*!
			@brief
				Selects the projection mode, keeping the existing parameters.
			@param mode
				Perspective or orthographic
			*/
		void setProjection(Projection mode);

		/*!
			@brief
				Sets the aspect ratio.

				Values that are not strictly positive are ignored - a minimized
				window reports a zero-height framebuffer, and feeding that to
				glm::perspective silently fills the matrix with NaNs that survive
				the restore and leave the screen black.
			@param aspect
				Width / height
			*/
		void setAspect(float aspect);

		/*!
			@brief
				Sets the vertical field of view.
			@param fovYRad
				Vertical field of view, in radians
			*/
		void setFovY(float fovYRad);

		/*!
			@brief
				Sets the near and far clip distances.
			@param nearZ
				Near clip distance
			@param farZ
				Far clip distance
			*/
		void setClip(float nearZ, float farZ);

		/*!
			@brief
				Sets the half height of the orthographic view volume.
			@param halfHeight
				Half the vertical extent, in world units
			*/
		void setOrthoHeight(float halfHeight);

		// Accessors

		/*!
			@brief
				World position of the eye.
			@return
				The camera position
			*/
		const glm::vec3& position() const	{ return position_; }

		/*!
			@brief
				Yaw angle.
			@return
				Yaw in radians
			*/
		float yaw() const					{ return yaw_; }

		/*!
			@brief
				Pitch angle.
			@return
				Pitch in radians
			*/
		float pitch() const					{ return pitch_; }

		/*!
			@brief
				Current projection mode.
			@return
				Perspective or orthographic
			*/
		Projection projectionMode() const	{ return mode_; }

		/*!
			@brief
				Vertical field of view.
			@return
				Field of view in radians
			*/
		float fovY() const					{ return fovY_; }

		/*!
			@brief
				Aspect ratio.
			@return
				Width / height
			*/
		float aspect() const				{ return aspect_; }

		/*!
			@brief
				Near clip distance.
			@return
				The near plane distance
			*/
		float nearZ() const					{ return nearZ_; }

		/*!
			@brief
				Far clip distance.
			@return
				The far plane distance
			*/
		float farZ() const					{ return farZ_; }

		/*!
			@brief
				Half height of the orthographic view volume.
			@return
				Half height in world units
			*/
		float orthoHeight() const			{ return orthoHeight_; }

		/*!
			@brief
				Camera forward axis.

				Basis vectors are read back out of the cached view matrix so
				that the trigonometry and the matrix can never disagree.
			@return
				Unit-length world-space axis
			*/
		glm::vec3 forward() const;

		/*!
			@brief
				Camera right axis, read back out of the cached view matrix.
			@return
				Unit-length world-space axis
			*/
		glm::vec3 right() const;

		/*!
			@brief
				Camera up axis, read back out of the cached view matrix.
			@return
				Unit-length world-space axis
			*/
		glm::vec3 up() const;

		// Derived matrices - recomputed on demand, then cached

		/*!
			@brief
				View matrix, recomputed on demand then cached.
			@return
				The view matrix
			*/
		const glm::mat4& view() const;

		/*!
			@brief
				Projection matrix, recomputed on demand then cached.
			@return
				The projection matrix
			*/
		const glm::mat4& projection() const;

		/*!
			@brief
				Projection * view, recomputed on demand then cached.
			@return
				The view-projection matrix
			*/
		const glm::mat4& viewProjection() const;

		// Culling

		/*!
			@brief
				The six view-volume planes, extracted from the view-projection.
			@return
				Inward-facing, normalised clipping planes
			*/
		const Frustum& frustum() const;

		/*!
			@brief
				Sphere / frustum overlap test.
			@param center
				Sphere centre in world space
			@param radius
				Sphere radius
			@return
				false only if the sphere is wholly outside the view volume
			*/
		bool intersects(const glm::vec3& center, float radius) const;

		/*!
			@brief
				Axis-aligned box / frustum overlap test.
			@param mn
				Minimum corner
			@param mx
				Maximum corner
			@return
				false only if the box is wholly outside the view volume
			*/
		bool intersects(const glm::vec3& mn, const glm::vec3& mx) const;

	private:

		glm::vec3			position_		{ 0.f, 0.f, 5.f };
		float				yaw_			{ 0.f };
		float				pitch_			{ 0.f };

		Projection			mode_			{ Projection::Perspective };
		float				fovY_			{ 1.0471976f };		// 60 degrees
		float				aspect_			{ 16.f / 9.f };
		float				nearZ_			{ 0.1f };
		float				farZ_			{ 1000.f };
		float				orthoHeight_	{ 10.f };

		// Caches. Mutable.
		mutable glm::mat4	view_			{ 1.f };
		mutable glm::mat4	proj_			{ 1.f };
		mutable glm::mat4	viewProj_		{ 1.f };
		mutable Frustum		frustum_		{};

		mutable bool		viewDirty_		{ true };
		mutable bool		projDirty_		{ true };
		mutable bool		viewProjDirty_	{ true };
		mutable bool		frustumDirty_	{ true };

		/*!
			@brief
				Rebuilds the view matrix from position, yaw and pitch.
			*/
		void recomputeView() const;

		/*!
			@brief
				Rebuilds the perspective or orthographic projection matrix.
			*/
		void recomputeProj() const;

		/*!
			@brief
				Rebuilds the view-projection matrix.
			*/
		void recomputeViewProj() const;

		/*!
			@brief
				Extracts the frustum planes from the view-projection.
			*/
		void recomputeFrustum() const;
	};
}
