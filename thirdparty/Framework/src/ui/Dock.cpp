#include "Dock.h"

using namespace fw;

void Dock::onInitialize() {
	_dockedRoot = addChild<View>("Docked Windows");
	_dockedRoot->setSizingPolicy(SizingPolicy::FitToParent);

	_floatingWinows = addChild<View>("Floating Dock Windows");
	_floatingWinows->setSizingPolicy(SizingPolicy::FitToParent);

	_overlay = addChild<DockOverlay>("Dock Overlay");
	_overlay->setSizingPolicy(SizingPolicy::FitToParent);
	_overlay->setViews(_dockedRoot, _floatingWinows);
}

void Dock::setRoot(ViewPtr root) {
	if (_dockedRoot != root) {
		removeChild(_dockedRoot);
	}

	addChild(root);
	_dockedRoot = root;
	root->pushToBack();

	root->setSizingPolicy(SizingPolicy::FitToParent);
	root->setDimensions(getDimensions());
}
