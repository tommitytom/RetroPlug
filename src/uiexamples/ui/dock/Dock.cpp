#include "Dock.h"

using namespace rp;

void Dock::onInitialized() {
	_dockedRoot = addChild<View>("Docked Windows");
	_dockedRoot->setSizingMode(SizingMode::FitToParent);

	_floatingWinows = addChild<View>("Floating Dock Windows");
	_floatingWinows->setSizingMode(SizingMode::FitToParent);

	_overlay = addChild<DockOverlay>("Dock Overlay");
	_overlay->setSizingMode(SizingMode::FitToParent);
	_overlay->setViews(_dockedRoot, _floatingWinows);
}

void Dock::setRoot(ViewPtr root) {
	if (_dockedRoot != root) {
		removeChild(_dockedRoot);
	}

	addChild(root);
	_dockedRoot = root;

	root->setSizingMode(SizingMode::FitToParent);
	root->setDimensions(getDimensions());
}
