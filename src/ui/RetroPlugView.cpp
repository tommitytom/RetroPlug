#include "RetroPlugView.h"

#include <sol/sol.hpp>

#include "foundation/FsUtil.h"
#include "foundation/Input.h"
#include "foundation/Math.h"
//#include "foundation/MetaFactory.h"
#include "foundation/StringUtil.h"
#include "foundation/SolUtil.h"

#include "core/ConfigLoader.h"
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

#include "ui/StartView.h"
#include "ui/SystemOverlayManager.h"
#include "ui/SystemView.h"
#include "ui/ViewManager.h"

#include "fonts/PlatNomor.h"

using namespace rp;

RetroPlugView::RetroPlugView(const fw::TypeRegistry& typeRegistry, const SystemFactory& systemFactory, IoMessageBus& messageBus):
	View({ 480, 432 }),
	_typeRegistry(typeRegistry),
	_project(typeRegistry, systemFactory, messageBus.allocator),
	_ioMessageBus(messageBus)
{
	setType<RetroPlugView>();
	setName("RetroPlug v0.4.0");
	setSizingPolicy(fw::SizingPolicy::FitToContent);
	ConfigLoader::loadConfig(_typeRegistry, "C:\\temp\\rpconfig\\config.lua", _config);
}

void RetroPlugView::onInitialize() {
	createState<SystemOverlayManager>();

	fw::FontDesc fontDesc;
	fontDesc.data.resize(PlatNomor_len);
	memcpy(fontDesc.data.data(), PlatNomor, PlatNomor_len);

	getResourceManager().create<fw::Font>("PlatNomor", fontDesc);

	_fileManager = this->createState<FileManager>();
	this->createState<Project>(entt::forward_as_any(_project));

	_compactLayout = this->addChild<CompactLayoutView>("Compact Layout");

	setupEventHandlers();
	getState<fw::EventNode>()->send("Audio"_hs, FetchStateRequest{});

	_nextStateFetch = _stateFetchInterval;
}

void RetroPlugView::setupEventHandlers() {
	fw::EventNode& node = *getState<fw::EventNode>();

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
		uint64 time = (uint64)std::chrono::high_resolution_clock::now().time_since_epoch().count();
		uint64 duration = time - ev.time;
		f32 ms = (f32)duration * 0.000001f;
		_lastPingTime = 0;
	});
}

void RetroPlugView::processOutput() {
	fw::EventNode& ev = *getState<fw::EventNode>();

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
	fw::EventNode& eventNode = *getState<fw::EventNode>();
	eventNode.update();

	if (_lastPingTime == 0) {
		_lastPingTime = (uint64)std::chrono::high_resolution_clock::now().time_since_epoch().count();
		eventNode.send("Audio"_hs, PingEvent{ .time = _lastPingTime });
	}

	f32 scale = _project.getScale();
	uint32 audioFrameCount = (uint32)(_sampleRate * delta + 0.5f);

	_compactLayout->setScale(scale);
	_compactLayout->setGridLayout((fw::GridLayout)_project.getState().settings.layout);

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
