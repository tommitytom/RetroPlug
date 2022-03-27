#include "SystemWrapper.h"

#include <spdlog/spdlog.h>

#include "core/ModelFactory.h"
#include "core/ProxySystem.h"
#include "core/SystemProcessor.h"
#include "sameboy/SameBoySystem.h"
#include "util/fs.h"

using namespace rp;

SystemPtr SystemWrapper::load(const SystemSettings& settings, LoadConfig&& loadConfig) {
	assert(loadConfig.romBuffer);

	bool alreadyInitialized = _system != nullptr;
	_models.clear();

	SystemManagerBase* manager = _processor->findManager<SameBoySystem>();

	SystemPtr system = manager->create(_systemId);
	std::string romName = manager->getRomName(*loadConfig.romBuffer);

	system->setStateCopyInterval(1000);

	ProxySystemPtr proxy = manager->createProxy(system->getId());

	_modelFactory->createModels(romName, _models);

	for (auto& model : _models) {
		model.second->onBeforeLoad(loadConfig);
	}

	system->load(std::forward<LoadConfig>(loadConfig));

	for (auto& model : _models) {
		model.second->onAfterLoad(system);
		model.second->setSystem(proxy);
	}

	proxy->handleSetup(system);

	if (!alreadyInitialized) {
		_messageBus->uiToAudio.enqueue(OrchestratorChange { .add = system });
	} else {
		_processor->removeSystem(_systemId);
		_messageBus->uiToAudio.enqueue(OrchestratorChange { .replace = system });
	}

	_processor->addSystem(proxy);

	_system = proxy;
	_settings = settings;
	_version++;

	return proxy;
}

void SystemWrapper::reset() {
	_messageBus->uiToAudio.enqueue(OrchestratorChange { .reset = _systemId });
}

bool SystemWrapper::saveSram(std::string_view path) {
	Uint8Buffer buffer;

	if (_system->saveSram(buffer)) {
		fs::path fullPath = path;

		if (fullPath.has_parent_path() && !fs::exists(fullPath.parent_path())) {
			if (!fs::create_directories(fullPath.parent_path())) {
				spdlog::error("Failed to write SRAM to {}.  Failed to create parent directory", path);
				return false;
			}
		}

		if (fsutil::writeFile(path, buffer)) {
			spdlog::info("Wrote SRAM to {}", path);
		} else {
			spdlog::error("Failed to write SRAM to {}", path);
		}
	}

	return false;
}
