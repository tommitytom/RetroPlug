#include "GridOverlay.h"

#include "foundation/StlUtil.h"

using namespace rp;

bool hasSystem(const std::vector<SystemViewPtr>& views, SystemPtr system) {
	for (SystemViewPtr systemView : views) {
		if (system == systemView->getSystem()) {
			return true;
		}
	}

	return false;
}

bool viewIsFocused(fw::ViewPtr view) {
	if (view->hasFocus()) {
		return true;
	}

	for (fw::ViewPtr child : view->getChildren()) {
		if (viewIsFocused(child)) {
			return true;
		}
	}

	return false;
}

void focusSystem(fw::ViewPtr view) {
	if (view->getChildren().size()) {
		view->getChildren().back()->focus();
	} else {
		view->focus();
	}
}

bool GridOverlay::onMouseButton(MouseButton::Enum button, bool down, fw::Point pos) {
	if (down) {
		std::vector<fw::ViewPtr>& children = _grid->getChildren();

		for (int32 i = (int32)children.size() - 1; i >= 0; --i) {
			if (children[i]->getWorldArea().contains(pos)) {
				setSelected((fw::ViewIndex)i);
				break;
			}
		}
	}

	return false;
}

void GridOverlay::onLayoutChanged() {
	std::vector<fw::ViewPtr>& children = _grid->getChildren();

	for (size_t i = 0; i < children.size(); ++i) {
		fw::ViewPtr view = children[i];

		if (viewIsFocused(view)) {
			_selected = (fw::ViewIndex)i;
		}
	}

	updateLayout();
}

void GridOverlay::onUpdate(f32 delta) {
	Project& project = getState<Project>();
	std::vector<fw::ViewPtr>& children = _grid->getChildren();

	if (_projectVersion == -1 || _projectVersion != project.getVersion()) {
		std::vector<SystemPtr>& systems = project.getSystems();

		std::vector<SystemViewPtr> systemViews;
		_grid->findChildren<SystemView>(systemViews);

		if (systems.empty()) {
			if (!_grid->findChild<StartView>()) {
				_grid->addChild<StartView>("Start View");
			}
		} else {
			_grid->removeChild<StartView>();
		}

		// Check for systems that were removed
		for (SystemViewPtr systemView : systemViews) {
			if (!fw::StlUtil::vectorContains(systems, systemView->getSystem())) {
				_grid->removeChild(systemView);

				// TODO: Also remove any related windows (like lsdj sample manager)
			}
		}

		for (SystemViewPtr systemView : systemViews) {
			if (systemView->versionIsDirty()) {
				systemView->removeChildren();

				SystemPtr system = systemView->getSystem();

				// TODO: Also remove any related windows (like lsdj sample manager)

				/*std::vector<fw::ViewPtr> overlays = getState<SystemOverlayManager>()->createOverlays(system->getRomName());

				for (fw::ViewPtr overlay : overlays) {
					overlay->setName(fmt::format("{} ({})", overlay->getName(), system->getRomName()));
					systemView->addChild(overlay);
				}*/

				systemView->updateVersion();
				_refocus = true;
			}
		}

		for (SystemPtr system : systems) {
			if (!hasSystem(systemViews, system)) {
				// New system was added

				std::string systemName = fmt::format("System {}", system->getId());

				SystemViewPtr systemView = _grid->addChild<SystemView>(systemName);
				systemView->setSystem(system);

				const SystemFactory& systemFactory = getState<const SystemFactory>();

				for (SystemServicePtr& service : system->getServices()) {
					fw::ViewPtr serviceView = systemFactory.createSystemServiceUi(service->getType());

					if (serviceView) {
						systemView->addChild(serviceView);
					} else {
						spdlog::debug("System service {} does not contain a UI", service->getType());
					}
				}

				/*std::vector<fw::ViewPtr> overlays = getState<SystemOverlayManager>()->createOverlays(system->getRomName());

				for (fw::ViewPtr overlay : overlays) {
					overlay->setName(fmt::format("{} ({})", overlay->getName(), systemName));
					systemView->addChild(overlay);
				}*/

				_refocus = true;
			}
		}

		_projectVersion = project.getVersion();
		//updateLayout();
	}
}

void GridOverlay::onRender(fw::Canvas& canvas) {
	if (_highlightMode == HighlightMode::Outline && _selected != fw::INVALID_VIEW_INDEX && _grid->getChildren().size() > 1) {
		fw::ViewPtr child = _grid->getChild(_selected);
		canvas.strokeRect((fw::DimensionF)child->getDimensions(), fw::Color4F::red);
	}
}

void GridOverlay::updateLayout() {
	if (getArea() != _grid->getArea()) {
		setArea(_grid->getArea());
	}

	std::vector<fw::ViewPtr>& children = _grid->getChildren();

	if (_selected == fw::INVALID_VIEW_INDEX && children.size() > 0) {
		_selected = 0;
	}

	if (_selected >= children.size()) {
		if (children.size() > 0) {
			_selected = (fw::ViewIndex)children.size() - 1;
		}
	}

	if (_selected != fw::INVALID_VIEW_INDEX && (_refocus || !viewIsFocused(children[_selected]))) {
		focusSystem(children[_selected]);
		_refocus = false;
	}

	f32 unselectedAlpha = 1.0f;
	if (_highlightMode == HighlightMode::Alpha) {
		unselectedAlpha = _unselectedAlpha;
	}

	for (size_t i = 0; i < children.size(); ++i) {
		if (i == _selected) {
			children[i]->setAlpha(1.0f);
		} else {
			children[i]->setAlpha(unselectedAlpha);
		}
	}
}
