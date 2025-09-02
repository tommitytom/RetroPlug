#include "RetroPlugApplication.h"

#include <spdlog/spdlog.h>

#include "foundation/OsPath.h"

#include "core/ConfigUtil.h"
#include "core/MgbService.h"
#include "core/Project.h"
#include "core/ProxySystem.h"
#include "core/RetroPlugConfig.h"
#include "core/RetroPlugProcessor.h"
#include "core/FileManager.h"
#include "sameboy/SameBoyFactory.h"
#include "lsdj/ArduinoboyServiceProvider.h"
#include "lsdj/LsdjServiceProvider.h"
#include "ui/RetroPlugView.h"
#include "ui/UiReflect.h"

RetroPlugApplication::RetroPlugApplication() {
	_typeRegistry.addCommonTypes();

	_typeRegistry.addType<fw::TypeId>();
	_typeRegistry.addType<entt::any>();
	_typeRegistry.addEnum<AudioChannelRouting>();
	_typeRegistry.addEnum<MidiChannelRouting>();
	_typeRegistry.addEnum<SystemLayout>();
	_typeRegistry.addEnum<SaveStateType>();

	fw::UiReflect::reflect(_typeRegistry);

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

	_typeRegistry.addType<SystemSettings>()
		.addField<&SystemSettings::includeRom>("includeRom")
		.addField<&SystemSettings::gameLink>("gameLink")
		.addField<&SystemSettings::reloadRomOnChange>("reloadRomOnChange")
		;

	_typeRegistry.addType<std::unordered_map<SystemServiceType, entt::any>>();

	_typeRegistry.addType<SystemDesc>()
		.addField<&SystemDesc::paths>("paths")
		.addField<&SystemDesc::services>("services")
		.addField<&SystemDesc::settings>("settings")
		;

	_typeRegistry.addType<std::vector<SystemDesc>>();

	_typeRegistry.addType<GlobalSettings>()
		.addField<&GlobalSettings::audioDeviceName>("audioDeviceName")
		.addField<&GlobalSettings::keyboard>("keyboard")
		.addField<&GlobalSettings::pad>("pad")
		;

	_typeRegistry.addType<RetroPlugConfig>()
		.addField<&RetroPlugConfig::settings>("settings")
		.addField<&RetroPlugConfig::project>("project")
		.addField<&RetroPlugConfig::system>("system");

	_typeRegistry.addEnum<LsdjSyncMode>();

	_typeRegistry.addType<ArduinoboyServiceSettings>()
		.addField<&ArduinoboyServiceSettings::autoPlay>("autoPlay")
		.addField<&ArduinoboyServiceSettings::syncMode>("syncMode")
		.addField<&ArduinoboyServiceSettings::tempoDivisor>("tempoDivisor")
		;

	_typeRegistry.addType<SampleSettings>()
		.addField<&SampleSettings::dither>("dither")
		.addField<&SampleSettings::volume>("volume")
		.addField<&SampleSettings::gain>("gain")
		.addField<&SampleSettings::pitch>("pitch")
		.addField<&SampleSettings::filter>("filter")
		.addField<&SampleSettings::cutoff>("cutoff")
		.addField<&SampleSettings::q>("q")
		;

	_typeRegistry.addType<KitSample>()
		.addField<&KitSample::name>("name")
		.addField<&KitSample::path>("path")
		.addField<&KitSample::settings>("settings")
		;

	_typeRegistry.addType<std::vector<KitSample>>();

	_typeRegistry.addType<KitState>()
		.addField<&KitState::name>("name")
		.addField<&KitState::samples>("samples")
		.addField<&KitState::settings>("settings")
		;

	//_typeRegistry.addType<KitIndex>();
	_typeRegistry.addType<std::unordered_map<KitIndex, KitState>>();

	_typeRegistry.addType<LsdjServiceSettings>()
		.addField<&LsdjServiceSettings::kits>("kits")
		.addField<&LsdjServiceSettings::kit>("kit")
		;

	_typeRegistry.addType<RecentFilePath>()
		.addField<&RecentFilePath::name>("name")
		.addField<&RecentFilePath::path>("path")
		.addField<&RecentFilePath::type>("type")
		;

	_typeRegistry.addType<std::vector<RecentFilePath>>();

	_systemFactory.addSystemProvider<SameBoyProvider>();
	_systemFactory.addSystemProvider<ProxyProvider>();
	_systemFactory.addSystemServiceProvider<ArduinoboyServiceProvider>();
	_systemFactory.addSystemServiceProvider<LsdjServiceProvider>();
	_systemFactory.addSystemServiceProvider<MgbServiceProvider>();

	ConfigUtil::initContent(_typeRegistry, _config);
}

fw::ViewPtr RetroPlugApplication::onCreateUi() {
	spdlog::info("Creating ui");
	return std::make_shared<RetroPlugView>(_typeRegistry, _systemFactory, _ioMessageBus, _config);
}

fw::AudioProcessorPtr RetroPlugApplication::onCreateAudio() {
	spdlog::info("Creating audio");
	return std::make_shared<RetroPlugProcessor>(fw::EventNode("Audio"), _typeRegistry, _systemFactory, _ioMessageBus, _config);
}
