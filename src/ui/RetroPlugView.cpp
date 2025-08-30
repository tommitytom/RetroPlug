#include "RetroPlugView.h"

#include <sol/sol.hpp>

#include "foundation/FsUtil.h"
#include "foundation/Input.h"
#include "foundation/Math.h"
#include "foundation/StringUtil.h"
#include "foundation/SolUtil.h"
#include "foundation/LuaScriptResource.h"

#include "core/ConfigUtil.h"
#include "core/Constants.h"
#include "core/Events.h"
#include "core/FileManager.h"
#include "core/InputManager.h"
#include "core/Project.h"
#include "core/ProjectSerializer.h"
#include "core/ProxySystem.h"
#include "core/RetroPlugConfig.h"
#include "core/System.h"
#include "core/SystemManager.h"
#include "core/SystemProcessor.h"
#include "core/SystemSettings.h"

#include "ui/DialogView.h"
#include "ui/MenuView.h"
#include "ui/PanelView.h"
#include "ui/StartView.h"
#include "ui/SystemOverlay.h"
#include "ui/SystemOverlayManager.h"
#include "ui/SystemView.h"
#include "ui/SwapContainerState.h"
#include "ui/UiEditOverlay.h"
#include "ui/ViewManager.h"
#include "ui/VerticalSplitter.h"

#include "lsdj/LsdjOverlay.h"

#include "fonts/PlatNomor.h"

#include "foundation/Any.h"
#include "foundation/LuaSerializer.h"

using namespace rp;

constexpr std::chrono::duration AUDIO_THREAD_TIMEOUT = std::chrono::milliseconds(500);

RetroPlugView::RetroPlugView(const fw::TypeRegistry& typeRegistry, const SystemFactory& systemFactory, IoMessageBus& messageBus, const RetroPlugConfig& config):
	View({ 480, 432 }),
	_typeRegistry(typeRegistry),
	_project(typeRegistry, systemFactory, messageBus.allocator),
	_ioMessageBus(messageBus),
	_inputManager(_fileManager),
	_config(config),
	_buttonWriter(_buttons),
	_fileManager(typeRegistry)
{
	setName(fmt::format("RetroPlug v{}", RP_VERSION));

	_inputManager.load(_config.settings.keyboard, InputType::Key);
	_inputManager.load(_config.settings.pad, InputType::Pad);
	//_gamepadManager.setAxisButtonThreshold(0.5f);
}

void RetroPlugView::initViews(SystemContainerViewPtr container) {
	this->removeChildren();
	this->getLayout().setOverflow(fw::FlexOverflow::Visible);

	if (container) {
		_systemContainer = addChild(container);
	} else {
		if (_project.getSystems().empty()) {
			_systemContainer = addChild<StartView>("Start View");
		} else {
			_systemContainer = std::make_shared<CompactLayoutView>(&_project);
			addChild(_systemContainer);
		}
	}

	_systemContainer->focus();
}

void RetroPlugView::onHotReload() {
	initViews(nullptr);
}

void RetroPlugView::onInitialize() {
	getLayout().setOverflow(fw::FlexOverflow::Visible);

	fw::audio::AudioManagerPtr* audioManagerPtr = tryGetState<fw::audio::AudioManagerPtr>();
	if (audioManagerPtr) {
		fw::audio::AudioManagerPtr& audioManager = *audioManagerPtr;
		std::vector<std::string> audioIn;
		std::vector<std::string> audioOut;
		audioManager->getDeviceNames(audioIn, audioOut);
		const int32 idx = fw::StlUtil::getVectorIndex(audioOut, _config.settings.audioDeviceName);
		audioManager->start(idx);
	}

	fw::FontDesc fontDesc;
	fontDesc.data.resize(PlatNomor_len);
	memcpy(fontDesc.data.data(), PlatNomor, PlatNomor_len);

	fw::ResourceManager& rm = getResourceManager();
	rm.create<fw::Font>("PlatNomor", fontDesc);

	this->createState<SystemOverlayManager>();
	this->createState<SwapContainerState>();
	this->createState(entt::forward_as_any(_project.getSystemFactory()));
	this->createState(entt::forward_as_any(_inputManager));
	this->createState(entt::forward_as_any(_fileManager));
	this->createState(entt::forward_as_any(_fileDialogManager));
	this->createState(entt::forward_as_any(_project));
	this->createState(entt::forward_as_any(_config));
	this->createState(entt::forward_as_any(_typeRegistry));

	initViews(nullptr);

	setupEventHandlers();
	getState<fw::EventNode>().send("Audio"_hs, FetchStateRequest{});

	_nextStateFetch = _stateFetchInterval;
/*
	_gamepadManager.setCallback([this](fw::PadButtonType button, bool down) {
		std::vector<std::string> actions;
		if (!_inputManager.processButton(button, down, _buttonWriter, actions)) {
			return;
		}

		// If a file dialog opens as the result of a key press, then this function will block until it is closed.
		// Because of this _buttons will never be properly cleared, so when the key up event in processed, it will
		// double process the key down event. To ensure this doesnt happen, we make a copy of the button presses to pass
		// to child elements, and clear the button list right away.
		std::vector<fw::StreamButtonPress> buttons = _buttonWriter.data();
		_buttonWriter.clear();

		_systemContainer->processInput(buttons, actions);
	});
*/
}

bool RetroPlugView::onKey(const fw::KeyEvent& ev) {
	if (ev.key == fw::VirtualKey::F1 && ev.down) {
		LsdjServiceSettings state = getLsdjState(1);
		getSystemServiceState(1, LSDJ_SERVICE_TYPE);
	}

	std::vector<std::string> actions;
	if (!_inputManager.processKey(ev.key, ev.down, _buttonWriter, actions)) {
		return false;
	}

	// If a file dialog opens as the result of a key press, then this function will block until it is closed.
	// Because of this _buttons will never be properly cleared, so when the key up event in processed, it will
	// double process the key down event. To ensure this doesnt happen, we make a copy of the button presses to pass
	// to child elements, and clear the button list right away.
	std::vector<fw::StreamButtonPress> buttons = _buttonWriter.data();
	_buttonWriter.clear();

	_systemContainer->processInput(buttons, actions);

	return true;
}

entt::any RetroPlugView::getSystemServiceState(SystemId id, SystemServiceType type) {
	if (_systemContainer->isType<CompactLayoutView>()) {
		auto grid = _systemContainer->asShared<CompactLayoutView>()->getGrid();

		std::vector<SystemViewPtr> systemViews;
		grid->findChildren<SystemView>(systemViews);

		for (const SystemViewPtr& systemView : systemViews) {
			SystemPtr system = systemView->getSystem();

			if (system && system->getId() == id) {
				for (size_t i = 0; i < systemView->getChildren().size(); i++) {
					auto child = std::static_pointer_cast<SystemOverlay>(systemView->getChildren()[i]);
					if (child->getServiceType() == type) {
						return child->getNode()->getSystemService()->getState();
					}
				}
			}
		}
	}

	return entt::any();
}

LsdjServiceSettings RetroPlugView::getLsdjState(SystemId id) {
	auto state = getSystemServiceState(id, LSDJ_SERVICE_TYPE);
	if (state) {
		return entt::any_cast<LsdjServiceSettings&>(state);
	}
	return LsdjServiceSettings();
}

void RetroPlugView::setupEventHandlers() {
	fw::EventNode& node = getState<fw::EventNode>();

	_project.setEventNode(node);

	node.receive<FetchStateResponse>([&](FetchStateResponse&& res) {
		spdlog::info("Received state response");
		_project.setup(getState<fw::EventNode>(), std::move(res));
	});

	node.receive<FetchSaveStateResponse>([&](FetchSaveStateResponse&& res) {
		SystemPtr system = _project.getSystemManager().findSystem(res.systemId);
		if (system) {
			system->setStateBuffer(std::move(res.state));
		}
	});

	node.receive<SystemIoPtr>([&](SystemIoPtr&& stream) {
		_project.getSystemManager().acquireIo(std::move(stream));
	});

	node.receive<CollectSystemEvent>([&](CollectSystemEvent&& ev) {
		// This ensures the system that has been removed is deallocated in the UI
		// thread rather than the audio thread.
		ev.system.reset();
	});

	node.receive<PongEvent>([&](PongEvent&& ev) {
		_lastPongTime = hrc::now();
		//std::chrono::nanoseconds duration = *_lastPongTime - ev.time;
		_lastPingTime = std::nullopt;
	});
}

void RetroPlugView::processOutput() {
	fw::EventNode& ev = getState<fw::EventNode>();

	for (SystemPtr& system : _project.getSystemManager().getSystems()) {
		SystemIoPtr io = system->getIo();
		if (io) {
			io->output.reset();
			ev.send("Audio"_hs, system->releaseIo());
		}

		// Prepare the system for the next frame
		SystemIoPtr nextIo = _ioMessageBus.alloc(system->getId());
		if (nextIo) {
			system->setIo(std::move(nextIo));
		}
	}
}

void RetroPlugView::updateWatchers() {
	for (const SystemPtr& system : _project.getSystems()) {
		const SystemDesc& desc = system->getDesc();
		auto found = _romWatchers.find(system->getId());

		// Check for systems that have been removed
		for (auto it = _romWatchers.begin(); it != _romWatchers.end();) {
			const SystemId systemId = it->first;
			auto found = std::find_if(_project.getSystems().begin(), _project.getSystems().end(), [systemId](const SystemPtr& system) {
				return system->getId() == systemId;
			});

			if (found == _project.getSystems().end()) {
				spdlog::info("Removing expired file watcher for {}", systemId);
				_fileManager.removeWatch(it->second);
				it = _romWatchers.erase(it);
			} else {
				++it;
			}
		}

		if (desc.settings.reloadRomOnChange) {
			if (found == _romWatchers.end()) {
				spdlog::info("Adding ROM watcher for system {} at {}", system->getId(), desc.paths.romPath);
				fw::WatchId watchId = _fileManager.startWatch(desc.paths.romPath, [&system, romPath = desc.paths.romPath](const std::string& path, fw::WatchAction action) {
					if (action == fw::WatchAction::Modified) {
						spdlog::info("Detected change in {}. Reloading!", romPath);
						fw::Uint8Buffer romBuffer;
						fw::FsUtil::readFile(romPath, &romBuffer);
						system->loadRom(std::move(romBuffer));
					}
				});

				_romWatchers[system->getId()] = watchId;
			}
		} else {
			if (found != _romWatchers.end()) {
				spdlog::info("Removing file watcher at {}", desc.paths.romPath);
				_fileManager.removeWatch(found->second);
				_romWatchers.erase(found);
			}
		}
	}
}

void RetroPlugView::updateThreadWarning(hrc::time_point time) {
	bool audioThreadActive = _lastPongTime.has_value() && (time - *_lastPongTime) < AUDIO_THREAD_TIMEOUT;
	if (audioThreadActive != _audioThreadActive) {
		_audioThreadActive = audioThreadActive;

		if (!audioThreadActive) {
			if (_threadWarning) _threadWarning->remove();
			_threadWarning = this->addChild<ThreadWarning>("Audio Thread Warning Panel");
		} else if (_threadWarning) {
			_threadWarning->remove();
			_threadWarning = nullptr;
		}
	}

	if (_threadWarning) {
		_threadWarning->bringToFront();
		_threadWarning->getChildAs<fw::LabelView>(0)->setFont("PlatNomor", 7 * _project.getScale());
	}
}

void RetroPlugView::onUpdate(f32 delta) {
	hrc::time_point time = hrc::now();

	fw::EventNode& eventNode = getState<fw::EventNode>();
	eventNode.update();

	getResourceManager().frame();
	_fileDialogManager.update();
	//_gamepadManager.update();
	_fileManager.update();

	if (_doPing && !_lastPingTime.has_value()) {
		_lastPingTime = time;
		eventNode.send("Audio"_hs, PingEvent{ .time = time });
	}

	updateWatchers();

	const f32 scale = _project.getScale();
	const uint32 audioFrameCount = (uint32)(_sampleRate * delta + 0.5f);

	SwapContainerState& swapContainer = getState<SwapContainerState>();
	if (swapContainer.requestedContainer) {
		initViews(swapContainer.requestedContainer);
		swapContainer.requestedContainer = nullptr;
	} else if (!_systemContainer
		|| _systemContainer->getParent() == nullptr
		|| (_systemContainer->isType<StartView>() && _project.getSystems().size())
		|| (!_systemContainer->isType<StartView>() && _project.getSystems().empty())
	) {
		initViews(nullptr);
	}

	_systemContainer->setScale(scale);
	//_compactLayout->setGridLayout((fw::GridLayout)_project.getState().settings.layout);

	updateThreadWarning(time);

	_project.update(audioFrameCount);

	if (_project.getState().settings.autoSave) {
		if (_project.saveIfRequired()) {
			_fileManager.addRecent(RecentFilePath{
				.type = "project",
				.name = _project.getName(),
				.path = _project.getState().path
			});
		}
	}

	_nextStateFetch -= delta;

	if (_nextStateFetch) {
		for (SystemPtr& system : _project.getSystems()) {
			eventNode.send("Audio"_hs, FetchSaveStateRequest{ .systemId = system->getId() });
		}

		_nextStateFetch = _stateFetchInterval;
	}

	const fw::ViewLayout& layout = _systemContainer->getLayout();
	fw::RectF area = fw::RectF(0, 0, layout.getMinWidth().getValue(), layout.getMinHeight().getValue());
	if (!std::isnan(area.w) && !std::isnan(area.h)) {
		area.w *= scale;
		area.h *= scale;
		this->setArea(fw::Rect(area));
	}
}

void RetroPlugView::onRender(fw::Canvas& canvas) {
	canvas.fillRect(getDimensions(), fw::Color4F(0, 0, 0, 1));
	processOutput();
}

bool RetroPlugView::onCloseWindowRequest(fw::CloseWindowContext& ctx) {
	if (_project.isDirty()) {
		DialogViewPtr dialog = std::make_shared<DialogView>("Save changes?", DialogType::YesNo);
		subscribe<DialogResult>(dialog, [&](const DialogResult& result) {
			if (result == DialogResult::Yes) {
				Project& project = getState<Project>();
				project.save();
			}

			this->requestClose();
		});

		SwapContainerState& swapContainer = getState<SwapContainerState>();
		swapContainer.requestedContainer = dialog;
		ctx.closing = false;
	}

	return true;
}
