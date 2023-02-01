#pragma once

#include "foundation/Resource.h"
#include "foundation/ResourceHandle.h"
#include "foundation/ResourceProvider.h"
#include "foundation/Types.h"
#include "graphics/Shader.h"
#include "graphics/ShaderProgram.h"

namespace fw {
	class GlShaderProgram : public ShaderProgram {
	private:
		uint32 _handle = 0;
		ShaderHandle _vertexShader;
		ShaderHandle _fragmentShader;

	public:
		GlShaderProgram(uint32 handle, ShaderHandle vertexShader, ShaderHandle fragmentShader)
			: _handle(handle), _vertexShader(vertexShader), _fragmentShader(fragmentShader) {}

		~GlShaderProgram();

		ShaderHandle getVertexShader() const {
			return _vertexShader;
		}

		ShaderHandle getFragmentShader() const {
			return _fragmentShader;
		}

		uint32 getGlHandle() const {
			return _handle;
		}
	};

	class GlShaderProgramProvider : public TypedResourceProvider<ShaderProgram> {
	private:
		const ResourceHandleLookup& _resources;
		std::shared_ptr<Resource> _vertexShader;
		std::shared_ptr<Resource> _fragmentShader;
		std::shared_ptr<Resource> _defaultProgram;

	public:
		GlShaderProgramProvider(const ResourceHandleLookup& lookup);
		~GlShaderProgramProvider() = default;

		std::shared_ptr<Resource> load(std::string_view uri) override { assert(false); return nullptr; }

		std::shared_ptr<Resource> create(const ShaderProgramDesc& desc, std::vector<std::string>& deps) override;
	};
}
