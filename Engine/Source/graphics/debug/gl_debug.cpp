/*************************************************************************************************************
\file       gl_debug.cpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      KHR_debug output in Debug builds: installs a synchronous callback that prints driver messages
            to stderr as "[gl] ...", filtering notifications and known NVIDIA chatter. Empty in Release.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#include "gl_debug.hpp"

// GLEW
#include <GL/glew.h>

// stds
#include <iostream>
#include <string_view>


namespace graphics::debug
{
#ifndef NDEBUG
	namespace
	{
		/*!
			@brief
				Short name for a KHR_debug message source.
			@param source
				GL_DEBUG_SOURCE_* value
			@return
				Static string naming the source
			*/
		const char* sourceName(GLenum source)
		{
			switch (source)
			{
			case GL_DEBUG_SOURCE_API:				return "api";
			case GL_DEBUG_SOURCE_WINDOW_SYSTEM:		return "window";
			case GL_DEBUG_SOURCE_SHADER_COMPILER:	return "shader";
			case GL_DEBUG_SOURCE_THIRD_PARTY:		return "third-party";
			case GL_DEBUG_SOURCE_APPLICATION:		return "app";
			case GL_DEBUG_SOURCE_OTHER:
			default:								return "other";
			}
		}

		/*!
			@brief
				Short name for a KHR_debug message type.
			@param type
				GL_DEBUG_TYPE_* value
			@return
				Static string naming the type
			*/
		const char* typeName(GLenum type)
		{
			switch (type)
			{
			case GL_DEBUG_TYPE_ERROR:				return "error";
			case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:	return "deprecated";
			case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:	return "undefined";
			case GL_DEBUG_TYPE_PORTABILITY:			return "portability";
			case GL_DEBUG_TYPE_PERFORMANCE:			return "performance";
			case GL_DEBUG_TYPE_MARKER:				return "marker";
			case GL_DEBUG_TYPE_OTHER:
			default:								return "other";
			}
		}

		/*!
			@brief
				Short name for a KHR_debug message severity.
			@param severity
				GL_DEBUG_SEVERITY_* value
			@return
				Static string naming the severity ("info" for anything else)
			*/
		const char* severityName(GLenum severity)
		{
			switch (severity)
			{
			case GL_DEBUG_SEVERITY_HIGH:			return "high";
			case GL_DEBUG_SEVERITY_MEDIUM:			return "medium";
			case GL_DEBUG_SEVERITY_LOW:				return "low";
			default:								return "info";
			}
		}

		/*!
			@brief
				KHR_debug callback: prints a message to stderr, skipping notifications
				and known NVIDIA informational ids.
			@param source
				Message source
			@param type
				Message type
			@param id
				Driver message id
			@param severity
				Message severity
			@param length
				Message length, or negative if null-terminated
			@param message
				Message text
			@param const void*
				User parameter, unused
			*/
		void GLAPIENTRY onMessage(GLenum source, GLenum type, GLuint id, GLenum severity,
			GLsizei length, const GLchar* message, const void* /*userParam*/)
		{
			if (severity == GL_DEBUG_SEVERITY_NOTIFICATION)
				return;

			// NVIDIA informational ids reported above notification severity:
			// buffer placement, texture-state reminders and the like.
			switch (id)
			{
			case 131169: case 131185: case 131204: case 131218:
				return;
			default:
				break;
			}

			const std::string_view text = length >= 0
				? std::string_view{ message, static_cast<std::size_t>(length) }
				: std::string_view{ message };

			std::cerr << "[gl] " << severityName(severity) << ' ' << typeName(type)
				<< " (" << sourceName(source) << ", id " << id << "): " << text << "\n";
		}
	}

	/*!
		@brief
			Routes the driver's KHR_debug messages to stderr as "[gl] ...".

			Does nothing without GL 4.3 / KHR_debug or on a context without the
			debug flag. Reports any errors already pending first, then enables
			synchronous debug output with no user pointer.
		*/
	void enableGlDebugOutput()
	{
		if (!(GLEW_VERSION_4_3 || GLEW_KHR_debug))
			return;

		GLint flags = 0;
		glGetIntegerv(GL_CONTEXT_FLAGS, &flags);

		if (!(flags & GL_CONTEXT_FLAG_DEBUG_BIT))
			return;

		// Report errors raised before this point rather than letting the next
		// glGetError caller take the blame for them.
		for (GLenum error = glGetError(); error != GL_NO_ERROR; error = glGetError())
			std::cerr << "[gl] error 0x" << std::hex << error << std::dec << " raised before debug output was enabled\n";

		glEnable(GL_DEBUG_OUTPUT);
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);

		// No userParam: messages can arrive while the window is torn down,
		// after anything it could point to is gone.
		glDebugMessageCallback(onMessage, nullptr);
		glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
	}

	/*!
		@brief
			Detaches the debug callback and disables debug output.
		*/
	void disableGlDebugOutput()
	{
		if (!(GLEW_VERSION_4_3 || GLEW_KHR_debug))
			return;

		glDebugMessageCallback(nullptr, nullptr);
		glDisable(GL_DEBUG_OUTPUT);
	}
#else
	/*!
		@brief
			Release build: does nothing.
		*/
	void enableGlDebugOutput() {}

	/*!
		@brief
			Release build: does nothing.
		*/
	void disableGlDebugOutput() {}
#endif
}
