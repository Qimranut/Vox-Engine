/*************************************************************************************************************
\file       layers.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Physics object and broad-phase layers: the single collision matrix (shouldCollide), query
            masks, the Jolt layer tables built from them, and mask-based query filters.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// Physics types
#include "../types/physics_types.hpp"

// Jolt
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h>

// stds
#include <cstdint>
#include <memory>


namespace physics::layers
{
	inline constexpr JPH::uint kNumObjectLayers = static_cast<JPH::uint>(Layer::Count);

	/**
	 * Broad-phase layers. Each is its own bounding volume tree, so bodies
	 * that move differently are kept apart: the static tree is rarely
	 * rebuilt, and a pile of debris does not bloat the tree the character
	 * sweeps against.
	 *
	 */
	namespace bp
	{
		inline constexpr JPH::BroadPhaseLayer NonMoving	{ 0 };
		inline constexpr JPH::BroadPhaseLayer Moving	{ 1 };
		inline constexpr JPH::BroadPhaseLayer Debris	{ 2 };
		inline constexpr JPH::BroadPhaseLayer Sensor	{ 3 };

		inline constexpr JPH::uint kCount = 4;
	}

	/*!
		@brief
			Converts an engine layer to its Jolt object layer.
		@param l
			Engine layer
		@return
			The Jolt object layer with the same index
		*/
	inline constexpr JPH::ObjectLayer toObjectLayer(Layer l)
	{
		return static_cast<JPH::ObjectLayer>(l);
	}

	/*!
		@brief
			THE collision matrix.

			Everything else - the Jolt pair table, the broad-phase filter, the
			self-check - is derived from this one function, so it cannot
			disagree with itself.

			              Static  Dynamic  Character  Debris  Sensor
			  Static        -        x         x         x       -
			  Dynamic       x        x         x         x       x
			  Character     x        x         x         -       x
			  Debris        x        x         -         -       -
			  Sensor        -        x         x         -       -
		@param a
			First layer
		@param b
			Second layer
		@return
			true when bodies on the two layers generate contacts
		*/
	inline constexpr bool shouldCollide(Layer a, Layer b)
	{
		const auto bit = [](Layer l) { return static_cast<std::uint32_t>(1u << static_cast<std::uint32_t>(l)); };

		const auto S = bit(Layer::Static);
		const auto D = bit(Layer::Dynamic);
		const auto C = bit(Layer::Character);
		const auto R = bit(Layer::Debris);
		const auto T = bit(Layer::Sensor);

		std::uint32_t mask = 0;

		switch (a)
		{
		case Layer::Static:		mask = D | C | R;			break;
		case Layer::Dynamic:	mask = S | D | C | R | T;	break;
		case Layer::Character:	mask = S | D | C | T;		break;
		case Layer::Debris:		mask = S | D;				break;
		case Layer::Sensor:		mask = D | C;				break;
		default:				mask = 0;					break;
		}

		return (mask & bit(b)) != 0;
	}

	/*!
		@brief
			Broad-phase layer an object layer lives in.
		@param l
			Engine layer
		@return
			The broad-phase tree the layer's bodies are inserted into
		*/
	inline constexpr JPH::BroadPhaseLayer broadPhaseOf(Layer l)
	{
		switch (l)
		{
		case Layer::Static:		return bp::NonMoving;
		case Layer::Debris:		return bp::Debris;
		case Layer::Sensor:		return bp::Sensor;
		case Layer::Dynamic:
		case Layer::Character:
		default:				return bp::Moving;
		}
	}

	/*!
		@brief
			Object-layer bitmask a query filter accepts.
		@param f
			Query filter
		@return
			Bit i set when Layer i may be hit
		*/
	inline constexpr std::uint32_t queryMask(QueryFilter f)
	{
		const auto bit = [](Layer l) { return static_cast<std::uint32_t>(1u << static_cast<std::uint32_t>(l)); };

		switch (f)
		{
		case QueryFilter::Camera:	return bit(Layer::Static) | bit(Layer::Dynamic);
		case QueryFilter::Gameplay:	return bit(Layer::Static) | bit(Layer::Dynamic) | bit(Layer::Character);
		case QueryFilter::All:
		default:					return (1u << kNumObjectLayers) - 1u;
		}
	}

	/**
	 * Owns the three tables PhysicsSystem::Init keeps REFERENCES to.
	 *
	 * They used to be locals in init(), which left the physics system holding
	 * dangling references the moment init() returned. Whatever owns these must
	 * outlive the PhysicsSystem.
	 *
	 */
	class LayerTables
	{
	public:

		/*!
			@brief
				Builds the broad-phase map, the pair filter and the
				object-vs-broad-phase table from shouldCollide and broadPhaseOf.
			*/
		LayerTables()
			:
			pairFilter_{ kNumObjectLayers },
			bpInterface_{ kNumObjectLayers, bp::kCount }
		{
			for (JPH::uint a = 0; a < kNumObjectLayers; ++a)
			{
				bpInterface_.MapObjectToBroadPhaseLayer(
					static_cast<JPH::ObjectLayer>(a),
					broadPhaseOf(static_cast<Layer>(a)));

				for (JPH::uint b = a; b < kNumObjectLayers; ++b)
				{
					if (shouldCollide(static_cast<Layer>(a), static_cast<Layer>(b)))
						pairFilter_.EnableCollision(
							static_cast<JPH::ObjectLayer>(a),
							static_cast<JPH::ObjectLayer>(b));
				}
			}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
			bpInterface_.SetBroadPhaseLayerName(bp::NonMoving, "NonMoving");
			bpInterface_.SetBroadPhaseLayerName(bp::Moving, "Moving");
			bpInterface_.SetBroadPhaseLayerName(bp::Debris, "Debris");
			bpInterface_.SetBroadPhaseLayerName(bp::Sensor, "Sensor");
#endif

			// Built last: its constructor reads the two tables above.
			objVsBp_ = std::make_unique<JPH::ObjectVsBroadPhaseLayerFilterTable>(
				bpInterface_, bp::kCount, pairFilter_, kNumObjectLayers);
		}

		LayerTables(const LayerTables&) = delete;
		LayerTables& operator=(const LayerTables&) = delete;

		/*!
			@brief
				Object-layer pair filter table.
			@return
				The table, owned by this object
			*/
		const JPH::ObjectLayerPairFilterTable&			pairFilter() const	{ return pairFilter_; }

		/*!
			@brief
				Object-to-broad-phase layer mapping.
			@return
				The table, owned by this object
			*/
		const JPH::BroadPhaseLayerInterfaceTable&		bpInterface() const	{ return bpInterface_; }

		/*!
			@brief
				Object-vs-broad-phase layer filter table.
			@return
				The table, owned by this object
			*/
		const JPH::ObjectVsBroadPhaseLayerFilterTable&	objVsBp() const		{ return *objVsBp_; }

	private:

		JPH::ObjectLayerPairFilterTable								pairFilter_;
		JPH::BroadPhaseLayerInterfaceTable							bpInterface_;
		std::unique_ptr<JPH::ObjectVsBroadPhaseLayerFilterTable>	objVsBp_;
	};

	/**
	 * Object-layer filter over a queryMask().
	 *
	 */
	class MaskObjectLayerFilter final : public JPH::ObjectLayerFilter
	{
	public:

		/*!
			@brief
				Stores the accepted object-layer mask.
			@param mask
				Bit i set when Layer i may be hit
			*/
		explicit MaskObjectLayerFilter(std::uint32_t mask) : mask_{ mask } {}

		/*!
			@brief
				Tests an object layer against the mask.
			@param layer
				Jolt object layer
			@return
				true when the layer's bit is set
			*/
		bool ShouldCollide(JPH::ObjectLayer layer) const override
		{
			return (mask_ & (1u << layer)) != 0;
		}

	private:

		std::uint32_t mask_;
	};

	/**
	 * Broad-phase filter that skips whole trees no layer in the mask lives in.
	 *
	 */
	class MaskBroadPhaseLayerFilter final : public JPH::BroadPhaseLayerFilter
	{
	public:

		/*!
			@brief
				Builds the broad-phase mask from the trees the masked object layers live in.
			@param objectMask
				Object-layer mask, as from queryMask()
			*/
		explicit MaskBroadPhaseLayerFilter(std::uint32_t objectMask)
		{
			for (JPH::uint l = 0; l < kNumObjectLayers; ++l)
			{
				if (objectMask & (1u << l))
					bpMask_ |= 1u << broadPhaseOf(static_cast<Layer>(l)).GetValue();
			}
		}

		/*!
			@brief
				Tests a broad-phase tree against the mask.
			@param layer
				Jolt broad-phase layer
			@return
				true when some masked object layer lives in that tree
			*/
		bool ShouldCollide(JPH::BroadPhaseLayer layer) const override
		{
			return (bpMask_ & (1u << layer.GetValue())) != 0;
		}

	private:

		std::uint32_t bpMask_ { 0 };
	};
}
