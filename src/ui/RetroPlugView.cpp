#include "RetroPlugView.h"

#include <sol/sol.hpp>

#include "foundation/FsUtil.h"
#include "foundation/Input.h"
#include "foundation/Math.h"
//#include "foundation/MetaFactory.h"
#include "foundation/StringUtil.h"
#include "foundation/SolUtil.h"
#include "foundation/LuaScriptResource.h"

#include "core/Events.h"
#include "core/FileManager.h"
#include "core/Project.h"
#include "core/System.h"
#include "core/SystemManager.h"
#include "core/SystemProcessor.h"
#include "core/SystemSettings.h"
#include "core/Project.h"
#include "core/ProjectSerializer.h"
#include "core/ProxySystem.h"

#include "ui/PanelView.h"
#include "ui/StartView.h"
#include "ui/SystemOverlayManager.h"
#include "ui/SystemView.h"
#include "ui/ViewManager.h"
#include "ui/VerticalSplitter.h"
#include "ui/PanelView.h"
#include "ui/UiEditOverlay.h"

#include "fonts/PlatNomor.h"
#include "foundation/LuaSerializer.h"

using namespace rp;

constexpr std::chrono::duration AUDIO_THREAD_TIMEOUT = std::chrono::milliseconds(500);

RetroPlugView::RetroPlugView(const fw::TypeRegistry& typeRegistry, const SystemFactory& systemFactory, IoMessageBus& messageBus):
	View({ 480, 432 }),
	_typeRegistry(typeRegistry),
	_project(typeRegistry, systemFactory, messageBus.allocator),
	_ioMessageBus(messageBus)
{
	setName("RetroPlug v0.4.0");
}

template <typename T>
void setupScriptWatch(const fw::TypeRegistry& reg, fw::ResourceReloader& reloader, std::string_view path, T& target) {
	fw::ResourceManager& rm = *reloader.getResourceManager();

	reloader.startWatch<fw::LuaScriptResource>(path, [&](const fw::LuaScriptHandle& handle) {
		fw::LuaSerializer::deserializeFromBuffer(reg, handle.getResource().getData(), target);
	});

	rm.load<fw::LuaScriptResource>(path);
}

void addTreeNodes(fw::TreeViewNode& node, fw::ViewPtr view) {
	node.name = view->getName();

	for (const auto& child : view->getChildren()) {
		addTreeNodes(node.children.emplace_back(), child);
	}
}

void RetroPlugView::initViews() {
	this->removeChildren();

	this->getLayout().setOverflow(fw::FlexOverflow::Visible);
	_compactLayout = this->addChild<CompactLayoutView>("Compact Layout");
	//_compactLayout->fitToParent();

	/*auto splitter = this->addChild<fw::DockSplitter>("Splitter");
	splitter->fitToParent();

	fw::ViewPtr inspectorContainer = splitter->addItem<fw::View>("InspectorPanel", 0);
	inspectorContainer->fitToParent();

	_viewTree = inspectorContainer->addChild<fw::TreeView>("UiTree");
	_viewTree->getLayout().setDimensions(fw::FlexDimensionValue{
		.width = fw::FlexValue(fw::FlexUnit::Percent, 100.0f),
		.height = fw::FlexValue(fw::FlexUnit::Auto)
	});

	_inspector = inspectorContainer->addChild<fw::ObjectInspectorView>("Inspector");
	_inspector->getLayout().setDimensions(fw::FlexDimensionValue{
		.width = fw::FlexValue(fw::FlexUnit::Percent, 100.0f),
		.height = fw::FlexValue(fw::FlexUnit::Auto)
	});

	_editContainer = splitter->addItem<fw::View>("UI Editor Container", 100);
	_editContainer->fitToParent();
	
	_compactLayout = _editContainer->addChild<CompactLayoutView>("Compact Layout");
	_compactLayout->fitToParent();

	std::shared_ptr<fw::UiEditOverlay> editOverlay = std::make_shared<fw::UiEditOverlay>(_typeRegistry, _inspector);
	_editContainer->addChild(editOverlay);

	editOverlay->setName("UI Overlay");
	editOverlay->fitToParent();
	editOverlay->setView(_editContainer);

	_inspector->addView(_typeRegistry, _compactLayout);*/
}

void RetroPlugView::onHotReload() {
	initViews();
}

void RetroPlugView::onInitialize() {
	addGlobalKeyHandler([&](const fw::KeyEvent& ev) {
		if (ev.down && ev.key == fw::VirtualKey::F5) {
			_viewTree->getRootNode().children.clear();
			addTreeNodes(_viewTree->getRootNode(), _editContainer);
			_viewTree->refresh();
		}

		return false;
	});

	getLayout().setOverflow(fw::FlexOverflow::Visible);
	//fitToParent();

	fw::ResourceManager& rm = getResourceManager();
	rm.addProvider<fw::LuaScriptResource, fw::LuaScriptProvider>();

	_resourceReloader.setResourceManager(rm);
	_resourceReloader.startWatch("C:\\temp\\rpconfig");

	setupScriptWatch(_typeRegistry, _resourceReloader, "C:\\temp\\rpconfig\\config.lua", _config);
	
	createState<SystemOverlayManager>();
	createState(entt::forward_as_any(_project.getSystemFactory()));

	fw::FontDesc fontDesc;
	fontDesc.data.resize(PlatNomor_len);
	memcpy(fontDesc.data.data(), PlatNomor, PlatNomor_len);
	
	rm.create<fw::Font>("PlatNomor", fontDesc);

	_fileManager = &this->createState<FileManager>();
	this->createState(entt::forward_as_any(_project));

	initViews();

	//_compactLayout = this->addChild<CompactLayoutView>("Compact Layout");

	setupEventHandlers();
	getState<fw::EventNode>().send("Audio"_hs, FetchStateRequest{});

	_nextStateFetch = _stateFetchInterval;
}

bool RetroPlugView::onKey(const fw::KeyEvent& ev) {
	if (ev.key == fw::VirtualKey::P && ev.down) {
		_doPing = !_doPing;
		return true;
	}

	return false;
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

	_resourceReloader.update();
	getResourceManager().frame();

	hrc::time_point time =  hrc::now();

	if (_doPing && !_lastPingTime.has_value()) {
		_lastPingTime = time;
		eventNode.send("Audio"_hs, PingEvent{ .time = time });
	}

	bool audioThreadActive = _lastPongTime.has_value() && (time - *_lastPongTime) < AUDIO_THREAD_TIMEOUT;
	if (audioThreadActive != _audioThreadActive) {
		_audioThreadActive = audioThreadActive;
		
		if (!audioThreadActive) {
			_threadWarning = this->addChild<fw::LabelView>("Audio Thread Warning");
			_threadWarning->getLayout().setDimensions(fw::Dimension{ 300, 100 });
			_threadWarning->setText("WARNING!");
		} else {
			if (_threadWarning) {
				_threadWarning->remove();
				_threadWarning = nullptr;
			}
		}
	}

	f32 scale = _project.getScale();
	uint32 audioFrameCount = (uint32)(_sampleRate * delta + 0.5f);

	_compactLayout->setScale(scale);
	//_compactLayout->setGridLayout((fw::GridLayout)_project.getState().settings.layout);

	_project.update(audioFrameCount);
	_project.saveIfRequired();

	_nextStateFetch -= delta;

	if (_nextStateFetch) {
		for (SystemPtr& system : _project.getSystems()) {
			eventNode.send("Audio"_hs, FetchSaveStateRequest{ .systemId = system->getId() });
		}

		_nextStateFetch = _stateFetchInterval;
	}
}

void RetroPlugView::onRender(fw::Canvas& canvas) {
	canvas.fillRect(getDimensions(), fw::Color4F(0, 0, 0, 1));
	processOutput();
}
