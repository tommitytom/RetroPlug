#pragma once

#include "foundation/Math.h"
#include "foundation/ResourceHandle.h"
#include "graphics/bgfx/BgfxResource.h"

namespace fw::engine {
	struct TextureDesc {
		Dimension dimensions;
		uint32 depth;
		std::vector<uint8> data;
	};

	class Texture : public Resource {
	public:
		using DescT = TextureDesc;

		Texture() : Resource(entt::type_id<Texture>()) {}
		~Texture() = default;
	};

	using TextureHandle = TypedResourceHandle<Texture>;
}
