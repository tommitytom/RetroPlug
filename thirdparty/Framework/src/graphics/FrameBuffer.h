#pragma once

#include "foundation/Math.h"
#include "foundation/ResourceHandle.h"

namespace fw::engine {
	struct FrameBufferDesc {
		Dimension dimensions;
		void* nwh = nullptr;
	};

	class FrameBuffer : public Resource {
	public:
		using DescT = FrameBufferDesc;

		FrameBuffer() : Resource(entt::type_id<FrameBuffer>()) {}
		~FrameBuffer() = default;

		virtual void setViewFrameBuffer(uint32 viewId) {}
	};

	using FrameBufferHandle = TypedResourceHandle<FrameBuffer>;
	using FrameBufferProvider = TypedResourceProvider<FrameBuffer>;
}
