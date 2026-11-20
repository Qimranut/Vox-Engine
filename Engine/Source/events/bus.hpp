/*************************************************************************************************************
\file       bus.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      The type_index-keyed publish/subscribe event bus (immediate, synchronous, re-entrancy-safe,
            main-thread only), its SubscriptionId handle and the ScopedSubscription RAII wrapper.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// stds
#include <vector>
#include <functional>
#include <unordered_map>
#include <typeindex>
#include <algorithm>
#include <cstdint>
#include <thread>
#include <cassert>


namespace events
{
	class Bus;

	// Handle to one subscription, returned by Bus::subscribe
	struct SubscriptionId
	{
		std::type_index	type_	{ typeid(void) };
		std::uint32_t	id_		{ 0 };

		/*!
			@brief
				Checks whether the handle refers to a real subscription.
			@return
				True when the id is nonzero
			*/
		bool valid() const
		{
			return id_ != 0;
		}
	};

	// type-indexed publish/subscribe bus
	class Bus
	{
	public:

		/*!
			@brief
				Creates an empty bus owned by the calling thread.
			*/
		Bus()
			:
			ownerThread_{ std::this_thread::get_id() }
		{}

		/*!
			@brief
				Registers a handler for one event type.
			@param handle
				Handler called with each published Event
			@return
				Handle for unsubscribe(); may be ignored
			*/
		template <typename Event>
		SubscriptionId subscribe(std::function<void(const Event&)> handle)
		{
			assertOwnerThread();

			const std::uint32_t id = ++subCount_;
			const std::type_index type{ typeid(Event) };

			subscribers_[type].push_back({ id, true, [handle](const void* e)
				{
					handle(*static_cast<const Event*>(e));
				} });

			return SubscriptionId{ type, id };
		}

		/*!
			@brief
				Cancels a subscription.

				Mid-dispatch the subscriber is only marked dead, and removed once
				the outermost publish finishes.
			@param s
				Handle returned by subscribe(); an invalid one is ignored
			*/
		void unsubscribe(const SubscriptionId& s)
		{
			if (!s.valid())
				return;

			assertOwnerThread();

			auto it = subscribers_.find(s.type_);
			if (it == subscribers_.end())
				return;

			for (auto& sub : it->second)
			{
				if (sub.id_ != s.id_)
					continue;

				sub.alive_ = false;

				if (dispatchDepth_ == 0)
					compact();
				else
					pendingCompact_ = true;

				return;
			}
		}

		/*!
			@brief
				Delivers an event to every live subscriber of its type, in
				subscription order.

				Subscribers added during the dispatch do not receive this event.
			@param e
				Event to deliver
			*/
		template <typename Event>
		void publish(const Event& e)
		{
			assertOwnerThread();

			auto it = subscribers_.find(std::type_index(typeid(Event)));
			if (it == subscribers_.end())
				return;

			auto& subs = it->second;

			// Snapshot the count so a handler subscribing to THIS type does
			// not receive the event it registered during, and index rather
			// than iterate so the vector may reallocate underneath us.
			const std::size_t count = subs.size();

			++dispatchDepth_;

			for (std::size_t i = 0; i < count; ++i)
			{
				if (subs[i].alive_)
					subs[i].handle_(&e);
			}

			--dispatchDepth_;

			// Erasing mid-dispatch would shift indices under an outer
			// publish, so removal waits for the outermost one to finish.
			if (dispatchDepth_ == 0 && pendingCompact_)
				compact();
		}

	private:

		struct Subscriber
		{
			std::uint32_t						id_;
			bool								alive_;
			std::function<void(const void*)>	handle_;
		};

		std::unordered_map<std::type_index, std::vector<Subscriber>> subscribers_;

		std::uint32_t	subCount_		{ 0 };

		// Nesting depth of publish(). Non-zero means a handler is running and
		// no vector may be resized.
		int				dispatchDepth_	{ 0 };
		bool			pendingCompact_	{ false };

		std::thread::id	ownerThread_;

		/*!
			@brief
				Physically removes every dead subscriber.
			*/
		void compact()
		{
			for (auto& [type, subs] : subscribers_)
			{
				subs.erase(
					std::remove_if(subs.begin(), subs.end(),
						[](const Subscriber& s) { return !s.alive_; }),
					subs.end());
			}

			pendingCompact_ = false;
		}

		/*!
			@brief
				Asserts that the caller is the thread that created the bus.
			*/
		void assertOwnerThread() const
		{
			assert(std::this_thread::get_id() == ownerThread_
				&& "events::Bus is main-thread only");
		}
	};

	// RAII wrapper that cancels its subscription on destruction.
	class ScopedSubscription
	{
	public:

		ScopedSubscription() = default;

		/*!
			@brief
				Takes ownership of a subscription.
			@param bus
				Bus the subscription was made on; must outlive this object
			@param id
				Handle returned by subscribe()
			*/
		ScopedSubscription(Bus& bus, const SubscriptionId& id)
			:
			bus_{ &bus },
			id_{ id }
		{}

		ScopedSubscription(const ScopedSubscription&) = delete;
		ScopedSubscription& operator=(const ScopedSubscription&) = delete;

		/*!
			@brief
				Takes over another object's subscription, leaving it empty.
			@param other
				Object to move from
			*/
		ScopedSubscription(ScopedSubscription&& other) noexcept
			:
			bus_{ other.bus_ },
			id_{ other.id_ }
		{
			other.bus_ = nullptr;
			other.id_ = {};
		}

		/*!
			@brief
				Cancels the current subscription, then takes over another object's,
				leaving it empty.
			@param other
				Object to move from
			@return
				This object
			*/
		ScopedSubscription& operator=(ScopedSubscription&& other) noexcept
		{
			if (this != &other)
			{
				reset();

				bus_ = other.bus_;
				id_ = other.id_;

				other.bus_ = nullptr;
				other.id_ = {};
			}

			return *this;
		}

		/*!
			@brief
				Cancels the subscription.
			*/
		~ScopedSubscription()
		{
			reset();
		}

		/*!
			@brief
				Cancels the subscription, if any, and leaves the object empty.
			*/
		void reset()
		{
			if (bus_ && id_.valid())
				bus_->unsubscribe(id_);

			bus_ = nullptr;
			id_ = {};
		}

	private:

		Bus*			bus_	{ nullptr };
		SubscriptionId	id_		{};
	};
}
