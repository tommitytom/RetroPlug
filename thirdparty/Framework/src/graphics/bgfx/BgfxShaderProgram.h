#pragma once

#include <bgfx/bgfx.h>

#include "foundation/Resource.h"
#include "foundation/ResourceHandle.h"
#include "foundation/ResourceProvider.h"
#include "foundation/Types.h"
#include "graphics/Shader.h"
#include "graphics/ShaderProgram.h"

namespace fw::engine {
	class BgfxShaderProgram : public ShaderProgram {
	private:
		bgfx::ProgramHandle _handle = { bgfx::kInvalidHandle };
		ShaderHandle _vertexShader;
		ShaderHandle _fragmentShader;

	public:
		BgfxShaderProgram(bgfx::ProgramHandle handle, ShaderHandle vertexShader, ShaderHandle fragmentShader)
			: _handle(handle), _vertexShader(vertexShader), _fragmentShader(fragmentShader) {}

		~BgfxShaderProgram() { 
			if (bgfx::isValid(_handle)) {
				bgfx::destroy(_handle);
			}
		}

		ShaderHandle getVertexShader() const {
			return _vertexShader;
		}

		ShaderHandle getFragmentShader() const {
			return _fragmentShader;
		}

		bgfx::ProgramHandle getBgfxHandle() const {
			return _handle;
		}
	};

	class BgfxShaderProgramProvider : public TypedResourceProvider<ShaderProgram> {
	private:
		const ResourceHandleLookup& _resources;
		std::shared_ptr<Resource> _vertexShader;
		std::shared_ptr<Resource> _fragmentShader;
		std::shared_ptr<Resource> _defaultProgram;

	public:
		BgfxShaderProgramProvider(const ResourceHandleLookup& lookup);
		~BgfxShaderProgramProvider() = default;

		std::shared_ptr<Resource> load(std::string_view uri) override { assert(false);  return nullptr; }

		std::shared_ptr<Resource> create(const ShaderProgramDesc& desc, std::vector<std::string>& deps) override;
	};
}
