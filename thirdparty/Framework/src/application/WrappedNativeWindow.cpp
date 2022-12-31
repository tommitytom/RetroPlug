#include "WrappedNativeWindow.h"

using namespace fw;
using namespace fw::app;

void WrappedNativeWindow::onCreate() {

}

void WrappedNativeWindow::onUpdate(f32 delta) {
	ViewManagerPtr vm = getViewManager();
	Dimension viewSize = vm->getDimensions();

	if (viewSize != _size) {
		if (vm->getSizingPolicy() == SizingPolicy::FitToContent) {
			// Resize window to fit content
			_size = viewSize;
		} else {
			// Resize content to fit window
			vm->setDimensions(_size);
		}
	}

	vm->onUpdate(delta);
}
