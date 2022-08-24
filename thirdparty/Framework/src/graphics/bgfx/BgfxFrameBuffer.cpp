#include "BgfxFrameBuffer.h"

using namespace fw;
using namespace fw::engine;

std::shared_ptr<Resource> BgfxFrameBufferProvider::create(const FrameBufferDesc& desc, std::vector<std::string>& deps) {
	bgfx::FrameBufferHandle handle = bgfx::createFrameBuffer(desc.nwh, desc.dimensions.w, desc.dimensions.h);
	return std::make_shared<BgfxFrameBuffer>(handle);
}

void BgfxFrameBuffer::setViewFrameBuffer(uint32 viewId) {
	bgfx::setViewFrameBuffer(viewId, getBgfxHandle());
}
