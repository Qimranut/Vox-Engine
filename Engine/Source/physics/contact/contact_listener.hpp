/*************************************************************************************************************
\file       contact_listener.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      ContactQueue: a Jolt contact listener that only enqueues ContactRecords under a mutex, for
            physics::System to drain and publish on the main thread after the step.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Jolt
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/ContactListener.h>

// GLM
#include <glm/glm.hpp>

// stds
#include <cstdint>
#include <mutex>
#include <vector>


namespace physics
{
	/**
	 * One raw contact change, as Jolt reported it.
	 *
	 * Sub-shape level and keyed by Jolt body id. physics::System folds these
	 * into body-pair events and resolves entities on the main thread.
	 *
	 */
	struct ContactRecord
	{
		enum class Kind : std::uint8_t
		{
			Added,
			Removed
		};

		Kind			kind_			{ Kind::Added };

		// JPH::BodyID::GetIndexAndSequenceNumber(), body1_ < body2_
		std::uint32_t	body1_			{ 0 };
		std::uint32_t	body2_			{ 0 };

		// Added only. User data is the entity; read here because the body
		// may be gone by the time the record is drained.
		std::uint64_t	user1_			{ 0 };
		std::uint64_t	user2_			{ 0 };
		glm::vec3		point_			{ 0.f };
		glm::vec3		normal_			{ 0.f };
		float			approachSpeed_	{ 0.f };
		bool			sensor_			{ false };
		bool			body1IsSensor_	{ false };
	};

	/**
	 * Jolt contact listener that ONLY enqueues.
	 *
	 * Jolt calls these from its job threads, several at once, with every body
	 * locked. Publishing on the bus from here would break the bus's
	 * main-thread rule and run gameplay handlers on worker threads with the
	 * world locked. So each callback copies what it needs into a record under
	 * a mutex, and the main thread drains the queue once the step is over.
	 *
	 */
	class ContactQueue final : public JPH::ContactListener
	{
	public:

		/*!
			@brief
				Jolt callback for a new contact; queues an Added record with point, normal and approach speed.
			@param body1
				First body of the pair
			@param body2
				Second body of the pair
			@param manifold
				Contact points and normal from Jolt
			@param settings
				Jolt contact settings (left unchanged)
			*/
		void OnContactAdded(const JPH::Body& body1,
							const JPH::Body& body2,
							const JPH::ContactManifold& manifold,
							JPH::ContactSettings& settings) override;

		/*!
			@brief
				Jolt callback for a lost contact; queues a Removed record by body id only.
			@param pair
				Sub-shape pair whose contact ended
			*/
		void OnContactRemoved(const JPH::SubShapeIDPair& pair) override;

		/*!
			@brief
				Moves every queued record out.
			@param out
				Cleared, then filled in the order Jolt reported
			*/
		void drain(std::vector<ContactRecord>& out);

	private:

		std::mutex					mtx_;
		std::vector<ContactRecord>	queue_;
	};
}
