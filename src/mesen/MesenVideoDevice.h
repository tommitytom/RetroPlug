#pragma once

#include "Core/Shared/Interfaces/IRenderingDevice.h"
#include "Core/Shared/RenderedFrame.h"
#include "foundation/Image.h"

#include <mutex>
#include <memory>

namespace rp {
	// IRenderingDevice implementation that latches the most recently decoded
	// video frame so it can be forwarded to io.output.video on the audio thread.
	class MesenVideoDevice final : public IRenderingDevice {
	public:
		// Called by VideoRenderer on the decode thread whenever a new frame is ready.
		// The decoded FrameBuffer contains Width * Height uint32_t pixels in
		// 0xAARRGGBB (ARGB) order.
		void UpdateFrame(RenderedFrame& frame) override {
			if (!frame.FrameBuffer) {
				return;
			}

			const uint32_t pixelCount = frame.Width * frame.Height;
			auto image = std::make_shared<orb::Image>((int32_t)frame.Width, (int32_t)frame.Height);

			const uint32_t* src = static_cast<const uint32_t*>(frame.FrameBuffer);
			orb::Color4*    dst = image->getData();

			// Mesen emits 0xAARRGGBB; orb::Color4 is {r, g, b, a}.
			for (uint32_t i = 0; i < pixelCount; ++i) {
				const uint32_t px = src[i];
				dst[i] = orb::Color4{
					static_cast<uint8>((px >> 16) & 0xFF),  // R
					static_cast<uint8>((px >>  8) & 0xFF),  // G
					static_cast<uint8>( px        & 0xFF),  // B
					static_cast<uint8>((px >> 24) & 0xFF)   // A
				};
			}

			std::lock_guard<std::mutex> lock(_mutex);
			_pendingFrame = std::move(image);
		}

		void ClearFrame() override {
			std::lock_guard<std::mutex> lock(_mutex);
			_pendingFrame = nullptr;
		}

		// No-ops: we don't drive a real render thread.
		void Render(RenderSurfaceInfo&, RenderSurfaceInfo&) override {}
		void Reset() override {}
		void SetExclusiveFullscreenMode(bool, void*) override {}

		// Take ownership of the latest pending frame (returns nullptr if none).
		orb::ImagePtr takeFrame() {
			std::lock_guard<std::mutex> lock(_mutex);
			return std::move(_pendingFrame);
		}

	private:
		mutable std::mutex _mutex;
		orb::ImagePtr      _pendingFrame;
	};
}
