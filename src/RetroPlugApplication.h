#pragma once

#include <entt/meta/container.hpp>

#include "core/Project.h"
#include "core/ProxySystem.h"
#include "core/RetroPlugProcessor.h"

#include "ui/RetroPlugView.h"

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
	RetroPlugApplication() {
		_typeRegistry.addCommonTypes();

		_typeRegistry.addType<fw::TypeId>();
		_typeRegistry.addType<entt::any>();
		_typeRegistry.addEnum<AudioChannelRouting>();
		_typeRegistry.addEnum<MidiChannelRouting>();
		_typeRegistry.addEnum<SystemLayout>();
		_typeRegistry.addEnum<SaveStateType>();

		_typeRegistry.addType<ProjectState::Settings>()
			.addField<&ProjectState::Settings::audioRouting>("audioRouting")
			.addField<&ProjectState::Settings::autoSave>("autoSave")
			.addField<&ProjectState::Settings::includeRom>("includeRom")
			.addField<&ProjectState::Settings::layout>("layout")
			.addField<&ProjectState::Settings::midiRouting>("midiRouting")
			.addField<&ProjectState::Settings::saveType>("saveType")
			.addField<&ProjectState::Settings::zoom>("zoom");

		_typeRegistry.addType<SystemPaths>()
			.addField<&SystemPaths::romPath>("romPath")
			.addField<&SystemPaths::sramPath>("sramPath");		

		_typeRegistry.addType<ProjectState>()
			.addField<&ProjectState::settings>("settings")
			//.addField<&ProjectState::path>("path")
			;

		_typeRegistry.addType<SystemSettings::InputSettings>()
			.addField<&SystemSettings::InputSettings::key>("key")
			.addField<&SystemSettings::InputSettings::pad>("pad");

		_typeRegistry.addType<SystemSettings>()
			.addField<&SystemSettings::includeRom>("includeRom")
			.addField<&SystemSettings::gameLink>("gameLink")
			.addField<&SystemSettings::input>("input");

		//_typeRegistry.addType<std::unordered_map<SystemServiceType, entt::any>>();

		_typeRegistry.addType<SystemDesc>()
			.addField<&SystemDesc::paths>("paths")
			.addField<&SystemDesc::services>("services")
			.addField<&SystemDesc::settings>("settings")
			;

		_typeRegistry.addType<std::vector<SystemDesc>>();

		_typeRegistry.addType<GlobalConfig>()
			.addField<&GlobalConfig::projectSettings>("project")
			.addField<&GlobalConfig::systemSettings>("system");

		_typeRegistry.addEnum<LsdjSyncMode>();

		_typeRegistry.addType<ArduinoboyServiceSettings>()
			.addField<&ArduinoboyServiceSettings::syncMode>("syncMode")
			.addField<&ArduinoboyServiceSettings::tempoDivisor>("tempoDivisor")
			;

		_systemFactory.addSystemProvider<SameBoyProvider>();
		_systemFactory.addSystemProvider<ProxyProvider>();
		_systemFactory.addSystemServiceProvider<ArduinoboyServiceProvider>();
		_systemFactory.addSystemServiceProvider<LsdjServiceProvider>();
		_systemFactory.addSystemServiceProvider<MgbServiceProvider>();
	}
	~RetroPlugApplication() = default;

	fw::ViewPtr onCreateUi() override {
		return std::make_shared<RetroPlugView>(_typeRegistry, _systemFactory, _ioMessageBus);
	}

	fw::AudioProcessorPtr onCreateAudio() override {
		return std::make_shared<RetroPlugProcessor>(_typeRegistry, _systemFactory, _ioMessageBus);
	}

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
