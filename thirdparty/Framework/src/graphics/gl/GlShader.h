#pragma once

#include "foundation/Resource.h"
#include "foundation/ResourceProvider.h"
#include "graphics/Shader.h"

namespace fw {
	class GlShader : public Shader {
	private:
		uint32 _handle = 0;

	public:
		GlShader(uint32 handle) : _handle(handle) {}
		~GlShader();

		uint32 getGlHandle() const {
			return _handle;
		}

		friend class GlShaderProvider;
	};

	class GlShaderProvider : public TypedResourceProvider<Shader> {
	public:
		GlShaderProvider() = default;
		~GlShaderProvider() = default;

		std::shared_ptr<Resource> load(std::string_view uri) override;

		std::shared_ptr<Resource> create(const ShaderDesc& desc, std::vector<std::string>& deps) override;
	};
}
