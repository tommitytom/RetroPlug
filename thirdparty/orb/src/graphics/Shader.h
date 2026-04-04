#pragma once

#include <entt/core/type_info.hpp>

#include "foundation/Types.h"
#include "foundation/Resource.h"
#include "foundation/ResourceHandle.h"
#include "foundation/DataBuffer.h"

namespace orb {
	enum class ShaderType {
		Unknown,
		Fragment,
		Vertex,
		Compute
	};

	struct ShaderDesc {
		Uint8Buffer data;
		ShaderType type = ShaderType::Unknown;
	};

	class Shader : public Resource {
	public:
		using DescT = ShaderDesc;

		Shader() : Resource(entt::type_id<Shader>()) {}
		~Shader() = default;
	};

	using ShaderHandle = TypedResourceHandle<Shader>;
}
