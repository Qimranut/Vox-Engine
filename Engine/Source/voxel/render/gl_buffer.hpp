/*************************************************************************************************************
\file       gl_buffer.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      GlBuffer: a move-only RAII wrapper over an OpenGL DSA buffer object, with immutable and
            mutable allocation.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once

// GLEW
#include <GL/glew.h>

// stds
#include <cstddef>
#include <utility>


namespace voxel::render
{
	/**
	 * A DSA buffer object with RAII. Immutable storage for the big static
	 * buffers, mutable data for the small per-frame ones.
	 *
	 */
	class GlBuffer
	{
	public:

		GlBuffer() = default;

		/*!
			@brief
				Deletes the GL buffer, if any.
			*/
		~GlBuffer() { release(); }

		/*!
			@brief
				Takes over another buffer's GL object, leaving it empty.
			@param o
				Buffer to move from
			*/
		GlBuffer(GlBuffer&& o) noexcept : id_(o.id_), size_(o.size_) { o.id_ = 0; o.size_ = 0; }

		/*!
			@brief
				Releases this buffer, then takes over another's GL object, leaving it empty.
			@param o
				Buffer to move from
			@return
				This buffer
			*/
		GlBuffer& operator=(GlBuffer&& o) noexcept
		{
			if (this != &o) { release(); id_ = o.id_; size_ = o.size_; o.id_ = 0; o.size_ = 0; }
			return *this;
		}

		GlBuffer(const GlBuffer&) = delete;
		GlBuffer& operator=(const GlBuffer&) = delete;

		/*!
			@brief
				Immutable storage; flags as for glNamedBufferStorage.

				Releases any previous buffer and creates a new one.
			@param bytes
				Size in bytes
			@param data
				Initial contents, or null
			@param flags
				glNamedBufferStorage flags
			*/
		void storage(std::size_t bytes, const void* data, GLbitfield flags)
		{
			release();
			glCreateBuffers(1, &id_);
			glNamedBufferStorage(id_, static_cast<GLsizeiptr>(bytes), data, flags);
			size_ = bytes;
		}

		/*!
			@brief
				Mutable (re)allocation; orphaning a per-frame buffer is fine.
			@param bytes
				Size in bytes
			@param data
				Initial contents, or null
			@param usage
				GL usage hint
			*/
		void data(std::size_t bytes, const void* data, GLenum usage)
		{
			if (id_ == 0)
				glCreateBuffers(1, &id_);
			glNamedBufferData(id_, static_cast<GLsizeiptr>(bytes), data, usage);
			size_ = bytes;
		}

		/*!
			@brief
				Uploads bytes into part of the buffer.
			@param offset
				Destination byte offset
			@param bytes
				Number of bytes
			@param data
				Source bytes
			*/
		void subData(std::size_t offset, std::size_t bytes, const void* data) const
		{
			glNamedBufferSubData(id_, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(bytes), data);
		}

		/*!
			@brief
				Deletes the GL buffer, if any, and resets the size.
			*/
		void release()
		{
			if (id_)
			{
				glDeleteBuffers(1, &id_);
				id_ = 0;
				size_ = 0;
			}
		}

		/*!
			@brief
				GL name of the buffer.
			@return
				The buffer id; 0 when none
			*/
		GLuint id() const { return id_; }

		/*!
			@brief
				Allocated size.
			@return
				Size in bytes
			*/
		std::size_t size() const { return size_; }

		/*!
			@brief
				Reports whether a GL buffer exists.
			@return
				true when the id is non-zero
			*/
		bool valid() const { return id_ != 0; }

	private:

		GLuint		id_		{ 0 };
		std::size_t	size_	{ 0 };
	};
}
