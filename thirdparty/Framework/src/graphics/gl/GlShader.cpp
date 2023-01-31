#include "GlShader.h"

#include <glad/gl.h>
#include <spdlog/spdlog.h>

#include "foundation/Types.h"
#include "foundation/FsUtil.h"
#include "graphics/gl/GlUtil.h"

namespace fw::engine {
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
				spdlog::error("Failed to load shader at {}, failed to open the file", uri);
			}
		} else {
			spdlog::error("Failed to load shader at {}, the file does not exist", uri);
		}

		return nullptr;
	}

	std::shared_ptr<Resource> GlShaderProvider::create(const ShaderDesc& desc, std::vector<std::string>& deps) {
		const GLchar* dataPtr = (const GLchar*)desc.data;
		GLint shaderSize = (GLint)desc.size;
		GLenum shaderType = getGlShaderType(desc.type);
		assert(shaderType != GL_INVALID_ENUM);

		uint32 shader = glCreateShader(shaderType);
		glShaderSource(shader, 1, &dataPtr, &shaderSize);
		glCompileShader(shader);

		if (!GlUtil::checkShaderCompileError(shader)) {
			return std::make_shared<GlShader>(shader);
		}

		return nullptr;
	}
}
