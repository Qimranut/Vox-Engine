/*************************************************************************************************************
\file       profiler_memory.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Global new/delete reported to Tracy. Compiled only with the CMake option VOX_PROFILER_MEMORY
            (off by default): every allocation in the process then pays for an event and, while a
            viewer is attached, a call stack. Turn it on to chase a leak or per-frame churn, read the
            viewer's Memory window, and turn it off again.
            What it sees: everything through operator new in Engine, Editor, Game and the header-only
            libraries (EnTT, sol2, glm, nlohmann/json, the STL). What it does not: malloc users - Lua's
            allocator, Jolt (its own Allocate hook), GLFW, FMOD, ImGui and the GL driver; those need
            VOX_PROFILE_ALLOC / VOX_PROFILE_FREE at their allocator hooks.
            The whole replaceable set is defined together, in one translation unit: a partial set would
            pair our new with the CRT's delete. Allocations made before a viewer connects are unknown
            to it (TRACY_ON_DEMAND), so it sees some frees without a matching allocation; the viewer
            tolerates that for on-demand captures.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "profiler.hpp"

namespace profile::detail
{
	/*!
		@brief
			Empty anchor function.

			Referenced from profile::startup so the linker always pulls this object
			out of Engine.lib, and with it the operators below, ahead of the CRT's.
		*/
	void linkMemoryHooks() {}
}

#if defined(VOX_PROFILER_ENABLED) && defined(VOX_PROFILER_MEMORY)

// stds
#include <cstdlib>
#include <malloc.h>
#include <new>


namespace
{
	// Frames kept per allocation while a viewer is attached. Deep enough to
	// get out of the STL and into our code; every extra frame costs time.
	constexpr int kCallstackDepth = 12;

	/*!
		@brief
			Allocates with malloc (at least one byte) and reports it to Tracy.
		@param bytes
			Requested size
		@return
			The block, or null on failure
		*/
	void* allocate(std::size_t bytes)
	{
		// malloc(0) may return null, new may not.
		void* ptr = std::malloc(bytes != 0 ? bytes : 1);

		if (ptr)
			TracyAllocS(ptr, bytes, kCallstackDepth);

		return ptr;
	}

	/*!
		@brief
			Allocates an aligned block (at least one byte) and reports it to Tracy.
		@param bytes
			Requested size
		@param alignment
			Required alignment
		@return
			The block, or null on failure
		*/
	void* allocateAligned(std::size_t bytes, std::align_val_t alignment)
	{
		void* ptr = _aligned_malloc(bytes != 0 ? bytes : 1, static_cast<std::size_t>(alignment));

		if (ptr)
			TracyAllocS(ptr, bytes, kCallstackDepth);

		return ptr;
	}

	/*!
		@brief
			Reports a free to Tracy and frees a block from allocate(). Null is ignored.
		@param ptr
			Block to free
		*/
	void release(void* ptr) noexcept
	{
		if (!ptr)
			return;

		TracyFreeS(ptr, kCallstackDepth);
		std::free(ptr);
	}

	/*!
		@brief
			Reports a free to Tracy and frees a block from allocateAligned(). Null is ignored.
		@param ptr
			Block to free
		*/
	void releaseAligned(void* ptr) noexcept
	{
		if (!ptr)
			return;

		TracyFreeS(ptr, kCallstackDepth);
		_aligned_free(ptr);
	}

	/*!
		@brief
			Throws std::bad_alloc for a null pointer.
		@param ptr
			Result of an allocation
		@return
			ptr, when not null
		*/
	void* orThrow(void* ptr)
	{
		if (!ptr)
			throw std::bad_alloc{};

		return ptr;
	}
}

/*!
	@brief
		Throwing scalar new, tracked.
	@param n
		Requested size
	@return
		The block
	*/
void* operator new(std::size_t n)												{ return orThrow(allocate(n)); }
/*!
	@brief
		Throwing array new, tracked.
	@param n
		Requested size
	@return
		The block
	*/
void* operator new[](std::size_t n)												{ return orThrow(allocate(n)); }
/*!
	@brief
		Non-throwing scalar new, tracked.
	@param n
		Requested size
	@param std::nothrow_t
		nothrow tag
	@return
		The block, or null
	*/
void* operator new(std::size_t n, const std::nothrow_t&) noexcept				{ return allocate(n); }
/*!
	@brief
		Non-throwing array new, tracked.
	@param n
		Requested size
	@param std::nothrow_t
		nothrow tag
	@return
		The block, or null
	*/
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept				{ return allocate(n); }

/*!
	@brief
		Throwing aligned scalar new, tracked.
	@param n
		Requested size
	@param a
		Required alignment
	@return
		The block
	*/
void* operator new(std::size_t n, std::align_val_t a)							{ return orThrow(allocateAligned(n, a)); }
/*!
	@brief
		Throwing aligned array new, tracked.
	@param n
		Requested size
	@param a
		Required alignment
	@return
		The block
	*/
void* operator new[](std::size_t n, std::align_val_t a)							{ return orThrow(allocateAligned(n, a)); }
/*!
	@brief
		Non-throwing aligned scalar new, tracked.
	@param n
		Requested size
	@param a
		Required alignment
	@param std::nothrow_t
		nothrow tag
	@return
		The block, or null
	*/
void* operator new(std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept	{ return allocateAligned(n, a); }
/*!
	@brief
		Non-throwing aligned array new, tracked.
	@param n
		Requested size
	@param a
		Required alignment
	@param std::nothrow_t
		nothrow tag
	@return
		The block, or null
	*/
void* operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept	{ return allocateAligned(n, a); }

/*!
	@brief
		Scalar delete, tracked.
	@param p
		Block to free
	*/
void operator delete(void* p) noexcept											{ release(p); }
/*!
	@brief
		Array delete, tracked.
	@param p
		Block to free
	*/
void operator delete[](void* p) noexcept										{ release(p); }
/*!
	@brief
		Sized scalar delete, tracked.
	@param p
		Block to free
	@param std::size_t
		Size of the block (unused)
	*/
void operator delete(void* p, std::size_t) noexcept								{ release(p); }
/*!
	@brief
		Sized array delete, tracked.
	@param p
		Block to free
	@param std::size_t
		Size of the block (unused)
	*/
void operator delete[](void* p, std::size_t) noexcept							{ release(p); }
/*!
	@brief
		Non-throwing scalar delete, tracked.
	@param p
		Block to free
	@param std::nothrow_t
		nothrow tag
	*/
void operator delete(void* p, const std::nothrow_t&) noexcept					{ release(p); }
/*!
	@brief
		Non-throwing array delete, tracked.
	@param p
		Block to free
	@param std::nothrow_t
		nothrow tag
	*/
void operator delete[](void* p, const std::nothrow_t&) noexcept					{ release(p); }

/*!
	@brief
		Aligned scalar delete, tracked.
	@param p
		Block to free
	@param std::align_val_t
		Alignment of the block (unused)
	*/
void operator delete(void* p, std::align_val_t) noexcept						{ releaseAligned(p); }
/*!
	@brief
		Aligned array delete, tracked.
	@param p
		Block to free
	@param std::align_val_t
		Alignment of the block (unused)
	*/
void operator delete[](void* p, std::align_val_t) noexcept						{ releaseAligned(p); }
/*!
	@brief
		Sized aligned scalar delete, tracked.
	@param p
		Block to free
	@param std::size_t
		Size of the block (unused)
	@param std::align_val_t
		Alignment of the block (unused)
	*/
void operator delete(void* p, std::size_t, std::align_val_t) noexcept			{ releaseAligned(p); }
/*!
	@brief
		Sized aligned array delete, tracked.
	@param p
		Block to free
	@param std::size_t
		Size of the block (unused)
	@param std::align_val_t
		Alignment of the block (unused)
	*/
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept			{ releaseAligned(p); }
/*!
	@brief
		Non-throwing aligned scalar delete, tracked.
	@param p
		Block to free
	@param std::align_val_t
		Alignment of the block (unused)
	@param std::nothrow_t
		nothrow tag
	*/
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept	{ releaseAligned(p); }
/*!
	@brief
		Non-throwing aligned array delete, tracked.
	@param p
		Block to free
	@param std::align_val_t
		Alignment of the block (unused)
	@param std::nothrow_t
		nothrow tag
	*/
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept	{ releaseAligned(p); }

#endif
