#include "GlShader.h"

#include <glad/gl.h>
#include <spdlog/spdlog.h>

#include "foundation/Types.h"
#include "foundation/FsUtil.h"
#include "graphics/gl/GlUtil.h"

namespace fw {
	GLenum getGlShaderType(ShaderType type) {
		switch (type) {
			case ShaderType::Fragment: return GL_FRAGMENT_SHADER;
			case ShaderType::Vertex: return GL_VERTEX_SHADER;
			//case ShaderType::Compute: return GL_COMPUTE_SHADER;
		}

		return GL_INVALID_ENUM;
	}

	ShaderType getShaderType(std::string_view ext) {
		if (ext == ".fs") {
			return ShaderType::Fragment;
		}

		if (ext == ".vs") {
			return ShaderType::Vertex;
		}

		return ShaderType::Unknown;
	}

	GlShader::~GlShader() {
		glDeleteShader(_handle);
	}

	std::shared_ptr<Resource> GlShaderProvider::load(std::string_view uri) {
		if (fs::exists(uri)) {
			std::vector<std::byte> fileData = fw::FsUtil::readFile(uri);

			if (fileData.size() > 0) {
				ShaderType shaderType = getShaderType(FsUtil::getFileExt(uri));
				assert(shaderType != ShaderType::Unknown);

				std::vector<std::string> deps;

				return create(ShaderDesc{
					.data = (uint8*)fileData.data(),
					.size = (uint32)fileData.size(),
					.type = shaderType
				}, deps);
			} else {
				printf("Failed to load shader at %s, failed to open the file\n", uri.data());
				spdlog::error("Failed to load shader at {}, failed to open the file", uri);
			}
		} else {
			printf("Failed to load shader at %s, failed to open the file\n", uri.data());
			spdlog::error("Failed to load shader at {}, the file does not exist", uri);
		}

		return nullptr;
	}

	std::shared_ptr<Resource> GlShaderProvider::create(const ShaderDesc& desc, std::vector<std::string>& deps) {
		printf("GlShaderProvider::create\n");
		// First verify we have a valid GL context
		if (glGetError() != GL_NO_ERROR) {
			printf("GL error exists before shader creation\n");
			return nullptr;
		}

		printf("desc.data\n");
		const GLchar* dataPtr = (const GLchar*)desc.data;
		GLint shaderSize = (GLint)desc.size;
		printf("getGlShaderType\n");
		GLenum shaderType = getGlShaderType(desc.type);

		printf("Creating shader of type 0x%x with size: %d\n", shaderType, shaderSize);

		if (shaderType == GL_INVALID_ENUM) {
			printf("Invalid shader type\n");
			return nullptr;
		}

		// Create shader with error checking
		GLuint shader = glCreateShader(shaderType);
		GLenum error = glGetError();
		
		if (error != GL_NO_ERROR || shader == 0) {
			printf("Failed to create shader: GL error 0x%x\n", error);
			return nullptr;
		}

		printf("Created shader %u\n", shader);

		glShaderSource(shader, 1, &dataPtr, &shaderSize);
		error = glGetError();
		if (error != GL_NO_ERROR) {
			printf("Error setting shader source: 0x%x\n", error);
			glDeleteShader(shader);
			return nullptr;
		}

		glCompileShader(shader);
		printf("Compilation complete\n");

		if (!GlUtil::checkShaderCompileError(shader)) {
			printf("Shader compilation succeeded\n");
			return std::make_shared<GlShader>(shader);
		}

		printf("Shader compilation failed\n");
		glDeleteShader(shader);
		return nullptr;
	}
}
