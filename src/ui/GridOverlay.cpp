#include "GridOverlay.h"

using namespace rp;

bool hasSystem(const std::vector<SystemViewPtr>& views, SystemWrapperPtr system) {
	for (SystemViewPtr systemView : views) {
		if (system == systemView->getSystem()) {
			return true;
		}
	}

	return false;
}

template <typename T>
bool vectorContains(const std::vector<T>& items, const T& item) {
	for (const T& t : items) {
		if (t == item) {
			return true;
		}
	}

	return false;
}

void GridOverlay::onUpdate(f32 delta) {
	Project* project = getShared<Project>();
	std::vector<ViewPtr>& children = _grid->getChildren();

	if (_projectVersion == -1 || _projectVersion != project->getVersion()) {
		std::vector<SystemWrapperPtr>& systems = project->getSystems();

		std::vector<SystemViewPtr> systemViews;
		_grid->findChildren<SystemView>(systemViews);

		if (systems.empty()) {
			_grid->addChild<StartView>("Start View");
		} else if (_grid->findChild<StartView>()) {
			_grid->removeChild<StartView>();
		}

		// Check for systems that were removed
		for (SystemViewPtr systemView : systemViews) {
			if (!vectorContains(systems, systemView->getSystem())) {
				_grid->removeChild(systemView);

				// TODO: Also remove any related windows (like lsdj sample manager)
			}
		}

		for (SystemViewPtr systemView : systemViews) {
			if (systemView->versionIsDirty()) {
				systemView->removeChildren();

				SystemWrapperPtr system = systemView->getSystem();

				// TODO: Also remove any related windows (like lsdj sample manager)

				std::vector<ViewPtr> overlays = getShared<SystemOverlayManager>()->createOverlays(system->getSystem()->getRomName());

				for (ViewPtr overlay : overlays) {
					overlay->setName(fmt::format("{} ({})", overlay->getName(), system->getSystem()->getRomName()));
					systemView->addChild(overlay);
				}

				systemView->updateVersion();
			}
		}

		for (SystemWrapperPtr system : systems) {
			if (!hasSystem(systemViews, system)) {
				// New system was added

				std::string systemName = fmt::format("System {}", system->getId());
				
				SystemViewPtr systemView = _grid->addChild<SystemView>(systemName);
				systemView->setSystem(system);

				std::vector<ViewPtr> overlays = getShared<SystemOverlayManager>()->createOverlays(system->getSystem()->getRomName());

				for (ViewPtr overlay : overlays) {
					overlay->setName(fmt::format("{} ({})", overlay->getName(), systemName));
					systemView->addChild(overlay);
				}
			}
		}

		_projectVersion = project->getVersion();
		//updateLayout();
	}
}
