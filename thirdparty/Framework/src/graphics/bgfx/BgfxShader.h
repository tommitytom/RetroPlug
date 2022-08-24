#pragma once

#include <bgfx/bgfx.h>

#include "foundation/Resource.h"
#include "foundation/ResourceProvider.h"
#include "graphics/Shader.h"
#include "graphics/bgfx/BgfxResource.h"

namespace fw::engine {
	using BgfxShader = BgfxResource<Shader, bgfx::ShaderHandle>;

	class BgfxShaderProvider : public TypedResourceProvider<Shader> {
	public:
		BgfxShaderProvider() = default;
		~BgfxShaderProvider() = default;

		std::shared_ptr<Resource> load(std::string_view uri) override;

		std::shared_ptr<Resource> create(const ShaderDesc& desc, std::vector<std::string>& deps) override;
	};
}
