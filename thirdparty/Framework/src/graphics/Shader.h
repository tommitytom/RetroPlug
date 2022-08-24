#pragma once

#include <entt/core/type_info.hpp>

#include "foundation/Types.h"
#include "foundation/Resource.h"
#include "foundation/ResourceHandle.h"

namespace fw::engine {
	struct ShaderDesc {
		const uint8* data = nullptr;
		uint32 size = 0;
	};

	class Shader : public Resource {
	public:
		using DescT = ShaderDesc;

		Shader() : Resource(entt::type_id<Shader>()) {}
		~Shader() = default;
	};

	using ShaderHandle = TypedResourceHandle<Shader>;
}
