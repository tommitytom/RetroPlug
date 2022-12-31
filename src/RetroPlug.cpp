#include "RetroPlug.h"

#include "foundation/SolUtil.h"

//#include "AudioContext.h"
#include <sol/sol.hpp>

#include "foundation/MetaFactory.h"
#include "foundation/Math.h"
#include "core/Project.h"
#include "core/System.h"
#include "core/SystemManager.h"
#include "core/SystemProcessor.h"
#include "core/SystemSettings.h"
#include "core/Proxies.h"
#include "core/UiState.h"
#include "ui/ViewManager.h"
#include "core/AudioStreamSystem.h"
#include "core/FileManager.h"
#include "foundation/Input.h"
#include "core/Project.h"
#include "core/ProjectSerializer.h"
#include "core/ProxySystem.h"
#include "sameboy/SameBoyManager.h"
#include "ui/SystemView.h"
#include "ui/StartView.h"
#include "ui/SystemOverlayManager.h"
#include "foundation/FsUtil.h"
#include "foundation/StringUtil.h"
#include "core/AudioContext.h"
#include "core/ConfigLoader.h"

#include "fonts/PlatNomor.h"

using namespace rp;

RetroPlug::RetroPlug() : View({ 480, 432 }) {
	setType<RetroPlug>();
	setSizingPolicy(fw::SizingPolicy::FitToContent);

	_typeRegistry.addCommonTypes();

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

	_typeRegistry.addType<SystemSettings::InputSettings>()
		.addField<&SystemSettings::InputSettings::key>("key")
		.addField<&SystemSettings::InputSettings::pad>("pad");

	_typeRegistry.addType<SystemSettings>()
		.addField<&SystemSettings::includeRom>("includeRom")
		.addField<&SystemSettings::gameLink>("gameLink")
		.addField<&SystemSettings::input>("input");

	_typeRegistry.addType<GlobalConfig>()
		.addField<&GlobalConfig::projectSettings>("project")
		.addField<&GlobalConfig::systemSettings>("system");

	ConfigLoader::loadConfig(_typeRegistry, "C:\\temp\\rpconfig\\config.lua", _config);
}

void RetroPlug::onInitialize() {
	FontDesc fontDesc;
	fontDesc.data.resize(PlatNomor_len);
	memcpy(fontDesc.data.data(), PlatNomor, PlatNomor_len);

	getResourceManager().create<Font>("PlatNomor", fontDesc);

	fw::audio::AudioManagerPtr& audioManager = *getState<fw::audio::AudioManagerPtr>();
	_audioContext = std::make_shared<AudioContext>(&_ioMessageBus, &_orchestratorMessageBus);
	audioManager->setProcessor(_audioContext);

	_state.processor.addManager(std::make_shared<SameBoyManager>());
	//_state.processor.addManager(std::make_shared<AudioStreamManager>());

	for (size_t i = 0; i < MAX_IO_STREAMS; ++i) {
		_ioMessageBus.allocator.enqueue(std::make_unique<SystemIo>());
	}

	_fileManager = this->createState<FileManager>();
	_project = this->createState<Project>();
	_project->setup(&_state.processor, &_orchestratorMessageBus);
	_project->setAudioManager(*audioManager);

	_project->getModelFactory().addModelFactory<LsdjModel>([](std::string_view romName) {
		std::string shortName = fw::StringUtil::toLower(romName).substr(0, 4);
		return shortName == "lsdj";
	});

	SystemOverlayManager* overlayManager = this->createState<SystemOverlayManager>();
	overlayManager->addOverlayFactory<LsdjOverlay>([](std::string_view romName) {
		std::string shortName = fw::StringUtil::toLower(romName).substr(0, 4);
		return shortName == "lsdj";
	});

	_state.compactLayout = this->addChild<CompactLayoutView>("Compact Layout");
}

void RetroPlug::processInput(uint32 frameCount) {
	std::vector<SystemPtr>& systems = _state.processor.getSystems();

	SystemIoPtr stream;
	while (_ioMessageBus.audioToUi.try_dequeue(stream)) {
		SystemPtr system = _state.processor.findSystem(stream->systemId);

		if (system) {
			if (system->getStream()) {
				system->getStream()->merge(*stream);
				_ioMessageBus.dealloc(std::move(stream));
			} else {
				system->setStream(std::move(stream));
			}
		} else {
			_ioMessageBus.dealloc(std::move(stream));
		}
	}
}

void RetroPlug::processOutput() {
	for (SystemPtr& system : _state.processor.getSystems()) {
		SystemIoPtr& io = system->getStream();
		if (io) {
			io->output.reset();

			// Pass IO buffers to audio thread
			_ioMessageBus.uiToAudio.try_enqueue(std::move(io));
		}

		// Prepare the system for the next frame
		SystemIoPtr nextIo = _ioMessageBus.alloc(system->getId());
		if (nextIo) {
			system->setStream(std::move(nextIo));
		}
	}
}

void RetroPlug::onUpdate(f32 delta) {
	f32 scale = _project->getScale();

	_state.compactLayout->setScale(scale);
	_state.compactLayout->setGridLayout((fw::GridLayout)_project->getState().settings.layout);

	uint32 frameCount = (uint32)(_sampleRate * delta + 0.5f);
	processInput(frameCount);

	_project->update(delta);

	_state.processor.process(frameCount);

	_project->saveIfRequired();
}

void RetroPlug::onRender(Canvas& canvas) {
	canvas.fillRect(getDimensions(), fw::Color4F(0, 0, 0, 1));
	processOutput();
}
