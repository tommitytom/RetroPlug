#include "UiContext.h"

#include <GLFW/glfw3.h>

#include "AudioContext.h"
#include "core/AudioStreamSystem.h"
#include "core/FileManager.h"
#include "core/Input.h"
#include "core/Project.h"
#include "core/ProjectSerializer.h"
#include "core/ProxySystem.h"
#include "sameboy/SameBoyManager.h"
#include "ui/SystemView.h"
#include "ui/StartView.h"
#include "ui/SystemOverlayManager.h"
#include "util/fs.h"
#include "util/StringUtil.h"

using namespace rp;

const f32 DISABLED_ALPHA = 0.75f;

UiContext::UiContext(IoMessageBus* messageBus, OrchestratorMessageBus* orchestratorMessageBus):
	_ioMessageBus(messageBus),
	_orchestratorMessageBus(orchestratorMessageBus)
{
	_state.processor.addManager<SameBoyManager>();
	_state.processor.addManager<SystemManager<AudioStreamSystem>>();

	for (size_t i = 0; i < MAX_IO_STREAMS; ++i) {
		messageBus->allocator.enqueue(std::make_unique<SystemIo>());
	}

	_fileManager = _state.viewManager.createShared<FileManager>();
	_project = _state.viewManager.createShared<Project>();
	_project->setup(&_state.processor, orchestratorMessageBus);

	_project->getModelFactory().addModelFactory<LsdjModel>([](std::string_view romName) {
		std::string shortName = StringUtil::toLower(romName).substr(0, 4);
		return shortName == "lsdj";
	});

	SystemOverlayManager* overlayManager = _state.viewManager.createShared<SystemOverlayManager>();
	overlayManager->addOverlayFactory<LsdjOverlay>([](std::string_view romName) {
		std::string shortName = StringUtil::toLower(romName).substr(0, 4);
		return shortName == "lsdj";
	});

	_state.grid = _state.viewManager.addChild<GridView>("Grid");
	_state.gridOverlay = _state.viewManager.addChild<GridOverlay>("Grid Overlay");
	_state.gridOverlay->setGrid(_state.grid);
}

void UiContext::setNvgContext(NVGcontext* vg) {
	_vg = vg;
	_state.viewManager.setVg(vg);
}

void UiContext::setAudioManager(AudioManager& audioManager) {
	_project->setAudioManager(audioManager);
}

void UiContext::processInput(uint32 frameCount) {
	std::vector<SystemPtr>& systems = _state.processor.getSystems();

	SystemIoPtr stream;
	while (_ioMessageBus->audioToUi.try_dequeue(stream)) {
		SystemPtr system = _state.processor.findSystem(stream->systemId);

		if (system) {
			if (system->getStream()) {
				system->getStream()->merge(*stream);
				_ioMessageBus->dealloc(std::move(stream));
			} else {
				system->setStream(std::move(stream));
			}
		} else {
			_ioMessageBus->dealloc(std::move(stream));
		}
	}
}

void UiContext::processOutput() {
	for (SystemPtr& system : _state.processor.getSystems()) {
		SystemIoPtr& io = system->getStream();
		if (io) {
			io->output.reset();

			// Pass IO buffers to audio thread
			_ioMessageBus->uiToAudio.try_enqueue(std::move(io));
		}

		// Prepare the system for the next frame
		SystemIoPtr nextIo = _ioMessageBus->alloc(system->getId());
		if (nextIo) {
			system->setStream(std::move(nextIo));
		}
	}
}

void UiContext::processDelta(f64 delta) {
	if (!_vg) {
		return;
	}

	if (delta >= 0.1) {
		delta = 0.1;
	}

	f32 scale = _project->getScale();
	_state.viewManager.setScale(scale);

	_state.grid->setLayoutMode((GridLayout)_project->getState().settings.layout);

	uint32 frameCount = (uint32)(_sampleRate * delta + 0.5);
	processInput(frameCount);

	_project->update((f32)delta);
	_state.viewManager.onUpdate((f32)delta);

	//_state.processor.process(frameCount);

	nvgSave(_vg);

	nvgScale(_vg, scale, scale);

	_state.viewManager.onRender();

	nvgRestore(_vg);

	processOutput();

	_project->saveIfRequired();
}

bool UiContext::onKey(VirtualKey::Enum key, bool down) {
	if (key == VirtualKey::Tab) {
		if (down) {
			_state.viewManager.findChild<GridOverlay>()->incrementSelection();
		}

		return true;
	}

	return _state.viewManager.onKey(key, down);
}

void UiContext::onMouseMove(double x, double y) {
	f32 scale = (f32)(_project->getState().settings.zoom + 1);

	x /= scale;
	y /= scale;
	_state.viewManager.onMouseMove({ (uint32)x, (uint32)y });
}

void UiContext::onMouseButton(MouseButton::Enum button, bool down) {
	_state.viewManager.onMouseButton(button, down);
}

void UiContext::onMouseScroll(double x, double y) {
	_state.viewManager.onMouseScroll(Point<f32> { (f32)x, (f32)y });
}

void UiContext::onDrop(int count, const char** paths) {
	std::vector<std::string> p;

	for (int i = 0; i < count; ++i) {
		p.push_back(paths[i]);
	}

	_state.viewManager.onDrop(p);
}

#include "roms/lsdjrom.h"
#include "roms/lsdjsav.h"

#include "sameboy/SameBoySystem.h"

SystemWrapperPtr sys;

void UiContext::onTouchStart(double x, double y) {
	if (!sys) {
		sys = _project->addSystem<SameBoySystem>(SystemSettings{
			.romPath = "lsdj-embedded.gb",
			.sramPath = "lsdj-embedded.sav"
		}, {
			.romBuffer = std::make_shared<Uint8Buffer>(LsdjRom, LsdjRom_len),
			.sramBuffer = std::make_shared<Uint8Buffer>(LsdjSav, LsdjSav_len)
		});
	} else {
		sys->getSystem()->setButtonState(ButtonType::Start, true);
	}
}

void UiContext::onTouchMove(double x, double y) {

}

void UiContext::onTouchEnd(double x, double y) {
	if (sys) {
		sys->getSystem()->setButtonState(ButtonType::Start, false);
	}
}

void UiContext::onTouchCancel(double x, double y) {

}
