#include "SystemOrchestrator.h"

#include "core/AudioState.h"
#include "core/ProxySystem.h"
#include "util/fs.h"

using namespace rp;

SystemPtr SystemOrchestrator::createUiSystem(SystemType systemType, std::string_view romPath, std::string_view sramPath) {
	/*SystemManagerBase* manager = _processor->findManager(systemType);
	assert(manager);

	SystemPtr system = manager->create();
	system->_id = _nextId++;

	SystemManagerBase* audioPlayerManager = _processor->findManager(entt::type_id<AudioStreamSystem>().seq());
	assert(audioPlayerManager);

	SystemPtr audioPlayerSystem = audioPlayerManager->create();
	audioPlayerSystem->_id = system->_id;

	Uint8Buffer romData;
	fsutil::readFile(romPath, &romData);
	system->loadRom(&romData);

	if (sramPath.size()) {
		Uint8Buffer sramData;
		fsutil::readFile(sramPath, &sramData);
		system->loadSram(&sramData, false);
	}

	_processor->addSystem(system);
	_messageBus->uiToAudio.enqueue(OrchestratorChange{ .add = audioPlayerSystem });

	return system;*/
	return nullptr;
}

SystemPtr SystemOrchestrator::createAudioSystem(SystemType systemType, LoadConfig&& loadConfig) {
	assert(loadConfig.romBuffer);
	
	SystemManagerBase* manager = _processor->findManager(systemType);
	assert(manager);
	
	SystemPtr system = manager->create(_nextId++);
	system->setStateCopyInterval(1000);

	system->load(std::forward<LoadConfig>(loadConfig));

	ProxySystemPtr proxy = manager->createProxy(system->getId());
	proxy->handleSetup(system);
	_processor->addSystem(proxy);

	_messageBus->uiToAudio.enqueue(OrchestratorChange { .add = system });

	return proxy;
}

SystemPtr SystemOrchestrator::createAudioSystem(SystemType systemType, std::string_view romPath, std::string_view sramPath) {
	LoadConfig loadConfig = LoadConfig {
		.romBuffer = std::make_shared<Uint8Buffer>(),
		.sramBuffer = std::make_shared<Uint8Buffer>()
	};

	if (!fsutil::readFile(romPath, loadConfig.romBuffer.get())) {
		return nullptr;
	}

	if (sramPath.size()) {
		loadConfig.sramBuffer = std::make_shared<Uint8Buffer>();
		if (!fsutil::readFile(sramPath, loadConfig.sramBuffer.get())) {
			// LOG
		}
	}

	return createAudioSystem(systemType, std::move(loadConfig));
}

void SystemOrchestrator::processIncoming() {
	OrchestratorChange change;
	while (_messageBus->audioToUi.try_dequeue(change)) {
		if (change.swap) {
			// TODO: Remove placeholder?
			_processor->addSystem(change.swap);
		}
	}
}

void SystemOrchestrator::resetSystem(SystemPtr system) {
	_messageBus->uiToAudio.enqueue(OrchestratorChange{ .reset = system->getId() });
}

void SystemOrchestrator::removeSystem(SystemId systemId) {
	_processor->removeSystem(systemId);
	_messageBus->uiToAudio.enqueue(OrchestratorChange{ .remove = systemId });
}

void SystemOrchestrator::removeSystem(SystemPtr system) {
	removeSystem(system->getId());
}

void SystemOrchestrator::removeAllSystems() {
	std::vector<SystemPtr> systems = _processor->getSystems();
	for (SystemPtr system : systems) {
		removeSystem(system);
	}
}
