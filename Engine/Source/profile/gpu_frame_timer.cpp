/*************************************************************************************************************
\file       gpu_frame_timer.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Implements GpuFrameTimer: issues GL_TIME_ELAPSED queries around a frame and polls finished
            results in order without stalling the GPU.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "gpu_frame_timer.hpp"

// third-party
#include <GL/glew.h>


namespace profile
{
	/*!
		@brief
			Deletes the query objects, if they were created.
		*/
	GpuFrameTimer::~GpuFrameTimer()
	{
		if (created_)
			glDeleteQueries(static_cast<GLsizei>(kQueries), queries_.data());
	}

	/*!
		@brief
			Begins a GL_TIME_ELAPSED query for this frame, unless every query is in flight.
		*/
	void GpuFrameTimer::begin()
	{
		if (!created_)
		{
			glGenQueries(static_cast<GLsizei>(kQueries), queries_.data());
			created_ = true;
		}

		// Every query still waiting on the GPU: skip this frame rather than
		// overwrite one whose result has not been read.
		if (inFlight_ == kQueries)
			return;

		glBeginQuery(GL_TIME_ELAPSED, queries_[write_]);
		open_ = true;
	}

	/*!
		@brief
			Ends the open query and advances the ring.
		*/
	void GpuFrameTimer::end()
	{
		if (!open_)
			return;

		glEndQuery(GL_TIME_ELAPSED);
		open_ = false;

		write_ = (write_ + 1) % kQueries;
		++inFlight_;
	}

	/*!
		@brief
			Reads every finished query, oldest first, stopping at the first not ready.
		@param ms
			Set to the newest finished measurement, in milliseconds
		@return
			True when ms was written
		*/
	bool GpuFrameTimer::poll(float& ms)
	{
		bool any = false;

		// Oldest first, and stop at the first one that is not ready: results
		// arrive in order.
		while (inFlight_ > 0)
		{
			GLint ready = GL_FALSE;
			glGetQueryObjectiv(queries_[read_], GL_QUERY_RESULT_AVAILABLE, &ready);

			if (ready == GL_FALSE)
				break;

			GLuint64 nanoseconds = 0;
			glGetQueryObjectui64v(queries_[read_], GL_QUERY_RESULT, &nanoseconds);

			ms = static_cast<float>(static_cast<double>(nanoseconds) / 1.0e6);
			any = true;

			read_ = (read_ + 1) % kQueries;
			--inFlight_;
		}

		return any;
	}
}
