#include "GlUtil.h"

#include <glad/glad.h>
#include <spdlog/spdlog.h>

namespace fw {
	std::string getShaderInfoLog(uint32 shader) {
		GLint maxLength = 0;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &maxLength);

		std::string errorLog;
		errorLog.resize(maxLength);
		glGetShaderInfoLog(shader, maxLength, &maxLength, errorLog.data());

		errorLog.resize(maxLength);

		return errorLog;
	}

	std::string getProgramInfoLog(uint32 program) {
		GLint maxLength = 0;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);

		std::string errorLog;
		errorLog.resize(maxLength);
		glGetProgramInfoLog(program, maxLength, &maxLength, errorLog.data());

		errorLog.resize(maxLength);

		return errorLog;
	}

	bool GlUtil::checkShaderCompileError(uint32 shader) {
		GLint status;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &status);

		if (status != GL_TRUE) {
			spdlog::error("Failed to compile shader: {}", getShaderInfoLog(shader));
			glDeleteShader(shader);
			return true;
		}

		return false;
	}

	bool GlUtil::checkProgramLinkError(uint32 program) {
		GLint status;
		glGetProgramiv(program, GL_LINK_STATUS, &status);

		if (status != GL_TRUE) {
			spdlog::error("Failed to link shader program: {}", getProgramInfoLog(program));
			glDeleteProgram(program);
			return true;
		}

		return false;
	}
}
