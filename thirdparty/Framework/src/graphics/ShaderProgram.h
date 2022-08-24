#pragma once

#include "foundation/Resource.h"
#include "foundation/ResourceHandle.h"

namespace fw::engine {
	struct ShaderProgramDesc {
		std::string vertexShader;
		std::string fragmentShader;
	};

	class ShaderProgram : public Resource {
	public:
		using DescT = ShaderProgramDesc;

		ShaderProgram() : Resource(entt::type_id<ShaderProgram>()) {}
		~ShaderProgram() = default;
	};

	using ShaderProgramHandle = TypedResourceHandle<ShaderProgram>;
}
