#pragma once

#include "foundation/TypeRegistry.h"
#include "core/RetroPlugConfig.h"
#include "core/System.h"
#include "core/SystemFactory.h"
#include "application/Application.h"

using namespace rp;

class RetroPlugApplication : public fw::app::Application {
private:
	IoMessageBus _ioMessageBus;
	fw::TypeRegistry _typeRegistry;
	SystemFactory _systemFactory;
	RetroPlugConfig _config;

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
