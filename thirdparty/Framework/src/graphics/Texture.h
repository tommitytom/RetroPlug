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
	protected:
		TextureDesc _desc;

	public:
		using DescT = TextureDesc;

		Texture(const TextureDesc& desc) : Resource(entt::type_id<Texture>()), _desc(desc) {}
		~Texture() = default;

		const TextureDesc& getDesc() const {
			return _desc;
		}
	};

	using TextureHandle = TypedResourceHandle<Texture>;
}
