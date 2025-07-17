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

#include "ui/PanelView.h"
#include "ui/StartView.h"
#include "ui/SystemOverlayManager.h"
#include "ui/SystemView.h"
#include "ui/ViewManager.h"
#include "ui/VerticalSplitter.h"
#include "ui/UiEditOverlay.h"
#include "ui/SwapContainerState.h"

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
	_buttonWriter(_buttons)
{
	setName(fmt::format("RetroPlug v{}", RP_VERSION));

	_inputManager.load(_config.settings.keyboard, InputType::Key);
	_inputManager.load(_config.settings.pad, InputType::Pad);
	_gamepadManager.setAxisButtonThreshold(0.5f);
}

void RetroPlugView::initViews(SystemContainerViewPtr container) {
	this->removeChildren();
	this->getLayout().setOverflow(fw::FlexOverflow::Visible);

	if (_project.getSystems().empty()) {
		_systemContainer = addChild<StartView>("Start View");
	} else {
		if (!container) {
			container = std::make_shared<CompactLayoutView>(&_project);
		}

		_systemContainer = addChild(container);
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
	this->createState(entt::forward_as_any(_project.getSystemFactory()));
	this->createState(entt::forward_as_any(_inputManager));
	this->createState(entt::forward_as_any(_fileManager));
	this->createState(entt::forward_as_any(_fileDialogManager));
	this->createState(entt::forward_as_any(_project));
	this->createState(entt::forward_as_any(_config));
	this->createState(entt::forward_as_any(_typeRegistry));
	this->createState<SwapContainerState>();

	initViews(nullptr);

	setupEventHandlers();
	getState<fw::EventNode>().send("Audio"_hs, FetchStateRequest{});

	_nextStateFetch = _stateFetchInterval;

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
}

bool RetroPlugView::onKey(const fw::KeyEvent& ev) {
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

void RetroPlugView::setupEventHandlers() {
	fw::EventNode& node = getState<fw::EventNode>();

	node.receive<FetchStateResponse>([&](FetchStateResponse&& res) {
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

void RetroPlugView::onUpdate(f32 delta) {
	fw::EventNode& eventNode = getState<fw::EventNode>();
	eventNode.update();

	getResourceManager().frame();
	_fileDialogManager.update();
	_gamepadManager.update();

	hrc::time_point time =  hrc::now();

	if (_doPing && !_lastPingTime.has_value()) {
		_lastPingTime = time;
		eventNode.send("Audio"_hs, PingEvent{ .time = time });
	}

	bool audioThreadActive = _lastPongTime.has_value() && (time - *_lastPongTime) < AUDIO_THREAD_TIMEOUT;
	if (audioThreadActive != _audioThreadActive) {
		_audioThreadActive = audioThreadActive;

		if (!audioThreadActive) {
			if (_threadWarning) {
				_threadWarning->remove();
			}

			_threadWarning = this->addChild<fw::PanelView>("Audio Thread Warning Panel");
			_threadWarning->setColor(fw::Color4(207, 39, 39, 240));
			_threadWarning->setBorderColor(fw::Color4F(1, 0, 0, 1));
			fw::ViewLayout& layout = _threadWarning->getLayout();

			layout.setFlexAlignItems(fw::FlexAlign::Center);
			layout.setJustifyContent(fw::FlexJustify::Center);
			layout.setFlexPositionType(fw::FlexPositionType::Absolute);
			layout.setHeight(fw::FlexValue::FlexValue(fw::FlexUnit::Percent, 10));
			layout.setWidth(fw::FlexValue::FlexValue(fw::FlexUnit::Percent, 90));
			layout.setPositionEdge(fw::FlexEdge::Left, fw::FlexValue::FlexValue(fw::FlexUnit::Percent, 5));
			layout.setPositionEdge(fw::FlexEdge::Bottom, fw::FlexValue::FlexValue(fw::FlexUnit::Percent, 5));

			auto text = _threadWarning->addChild<fw::LabelView>("Audio Thread Warning Text");
			text->setText("Audio thread inactive - check settings");
			text->setFont("PlatNomor", 7 * _project.getScale());
		} else if (_threadWarning) {
			_threadWarning->remove();
			_threadWarning = nullptr;
		}
	}

	f32 scale = _project.getScale();
	uint32 audioFrameCount = (uint32)(_sampleRate * delta + 0.5f);

	SwapContainerState& swapContainer = getState<SwapContainerState>();
	if (swapContainer.requestedContainer) {
		initViews(swapContainer.requestedContainer);
	} else if (!_systemContainer || _systemContainer->getParent() == nullptr || (_systemContainer->isType<StartView>() && _project.getSystems().size())) {
		initViews(nullptr);
	}

	_systemContainer->setScale(scale);
	//_compactLayout->setGridLayout((fw::GridLayout)_project.getState().settings.layout);

	if (_threadWarning) {
		_threadWarning->getChildAs<fw::LabelView>(0)->setFont("PlatNomor", 7 * _project.getScale());
	}

	_project.update(audioFrameCount);

	if (_project.getState().settings.autoSave) {
		_project.saveIfRequired();

		_fileManager.addRecent(RecentFilePath{
			.type = "project",
			.name = _project.getName(),
			.path = _project.getState().path
		});
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
	return true;
}
