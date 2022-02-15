#include "SystemWrapper.h"

#include "core/ModelFactory.h"
#include "core/ProxySystem.h"
#include "core/SystemProcessor.h"
#include "sameboy/SameBoySystem.h"

using namespace rp;

SystemPtr SystemWrapper::load(LoadConfig&& loadConfig) {
	assert(loadConfig.romBuffer);

	bool alreadyInitialized = _system != nullptr;
	_models.clear();

	SystemManagerBase* manager = _processor->findManager<SameBoySystem>();

	SystemPtr system = manager->create(_systemId);
	std::string romName = manager->getRomName(*loadConfig.romBuffer);

	system->setStateCopyInterval(1000);

	_modelFactory->createModels(romName, _models);

	for (auto& model : _models) {
		model.second->onBeforeLoad(loadConfig);
	}

	system->load(std::forward<LoadConfig>(loadConfig));

	for (auto& model : _models) {
		model.second->onAfterLoad(system);
	}

	ProxySystemPtr proxy = manager->createProxy(system->getId());
	proxy->handleSetup(system);
	_processor->addSystem(proxy);

	if (!alreadyInitialized) {
		_messageBus->uiToAudio.enqueue(OrchestratorChange { .add = system });
	} else {
		_messageBus->uiToAudio.enqueue(OrchestratorChange { .replace = system });
	}

	_system = proxy;

	return proxy;
}

void SystemWrapper::reset() {
	_messageBus->uiToAudio.enqueue(OrchestratorChange { .reset = _systemId });
}
