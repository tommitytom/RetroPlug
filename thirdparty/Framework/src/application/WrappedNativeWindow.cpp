#include "WrappedNativeWindow.h"

using namespace fw;
using namespace fw::app;

void WrappedNativeWindow::onCreate() {

}

void WrappedNativeWindow::onUpdate(f32 delta) {
	ViewManagerPtr vm = getViewManager();

	Dimension viewSize = vm->getDimensions();

	// NOTE: An ID of 0 is always given to the main window.  It does not need a new frame buffer.
	bool resizeFrameBuffer = getId() > 0 && !_frameBuffer;

	if (viewSize != _size) {
		if (vm->getSizingPolicy() == SizingPolicy::FitToContent) {
			// Resize window to fit content
			_size = viewSize;
			//glfwSetWindowSize(_window, (int)viewSize.w, (int)viewSize.h);
		} else {
			// Resize content to fit window
			vm->setDimensions(_size);
			viewSize = _size;
		}

		if (getId() == 0) {
			bgfx::reset((uint32_t)viewSize.w, (uint32_t)viewSize.h, 0);
		} else {
			resizeFrameBuffer = true;
		}

		bgfx::setViewRect(getId(), 0, 0, bgfx::BackbufferRatio::Equal);
	}

	if (resizeFrameBuffer && _frameBufferProvider) {
		_frameBuffer = _frameBufferProvider->createTyped(FrameBufferDesc{
			.dimensions = _size,
			.nwh = getNativeHandle(),
		});

		_frameBuffer->setViewFrameBuffer(getId());
	}

	vm->onUpdate(delta);
}
