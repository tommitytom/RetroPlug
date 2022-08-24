#pragma once

#include "foundation/Math.h"
#include "foundation/ResourceProvider.h"
#include "graphics/FrameBuffer.h"
#include "graphics/bgfx/BgfxResource.h"

namespace fw::engine {
	class BgfxFrameBuffer final : public BgfxResource<FrameBuffer, bgfx::FrameBufferHandle> {
	public:
		BgfxFrameBuffer() = default;
		BgfxFrameBuffer(bgfx::FrameBufferHandle handle): BgfxResource(handle) {}
		~BgfxFrameBuffer() = default;

		void setViewFrameBuffer(uint32 viewId) override;
	};
	
	class BgfxFrameBufferProvider final : public TypedResourceProvider<FrameBuffer> {
	public:
		std::shared_ptr<Resource> load(std::string_view uri) override { assert(false); return nullptr; }

		std::shared_ptr<Resource> create(const FrameBufferDesc& desc, std::vector<std::string>& deps) override;
	};
}
