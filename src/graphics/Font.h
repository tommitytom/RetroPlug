#pragma once

#include "RpMath.h"
#include "foundation/Resource.h"
#include "foundation/ResourceHandle.h"

namespace rp::engine {
	struct FontDesc {
		std::vector<std::byte> data;
	};

	class Font : public Resource {
	public:
		using DescT = FontDesc;

		Font() : Resource(entt::type_id<Font>()) {}
		~Font() = default;
	};

	using FontHandle = TypedResourceHandle<Font>;
}
