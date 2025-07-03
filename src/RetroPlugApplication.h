#pragma once

//#include <entt/meta/container.hpp>

#include "core/Project.h"
#include "core/ProxySystem.h"
#include "core/RetroPlugProcessor.h"

#include "ui/RetroPlugView.h"
#include "ui/UiReflect.h"

#include "application/Application.h"

#include "sameboy/SameBoyFactory.h"
#include "lsdj/LsdjServiceProvider.h"
#include "core/MgbService.h"

using namespace rp;

class RetroPlugApplication : public fw::app::Application {
private:
	IoMessageBus _ioMessageBus;
	fw::TypeRegistry _typeRegistry;
	SystemFactory _systemFactory;

public:
	RetroPlugApplication();
	~RetroPlugApplication() = default;

	fw::ViewPtr onCreateUi() override;

	fw::AudioProcessorPtr onCreateAudio() override;

	/*void onInitialize(fw::app::UiContext& view, fw::audio::AudioManagerPtr audio) override {
		_project.getModelFactory().addModelFactory<LsdjModel>([](std::string_view romName) {
			std::string shortName = fw::StringUtil::toLower(romName).substr(0, 4);
			return shortName == "lsdj";
		});

		SystemOverlayManager* overlayManager = view.getMainWindow()->getViewManager()->createState<SystemOverlayManager>();
		overlayManager->addOverlayFactory<LsdjOverlay>([](std::string_view romName) {
			std::string shortName = fw::StringUtil::toLower(romName).substr(0, 4);
			return shortName == "lsdj";
		});
	}*/
};
