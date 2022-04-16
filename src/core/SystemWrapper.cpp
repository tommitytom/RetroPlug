#include "SystemWrapper.h"

#include <sol/sol.hpp>
#include <spdlog/spdlog.h>

#include "core/ModelFactory.h"
#include "core/ProxySystem.h"
#include "core/SystemProcessor.h"
#include "sameboy/SameBoySystem.h"
#include "util/fs.h"
#include "util/MetaUtil.h"
#include "util/SolUtil.h"

using namespace rp;

SystemPtr SystemWrapper::load(const SystemSettings& settings, LoadConfig&& loadConfig) {
	assert(loadConfig.romBuffer);

	bool alreadyInitialized = _system != nullptr;
	_models.clear();
	_settings = settings;

	SystemManagerBase* manager = _processor->findManager<SameBoySystem>();

	SystemPtr system = manager->create(_systemId);
	std::string romName = manager->getRomName(*loadConfig.romBuffer);

	system->setStateCopyInterval(1000);

	ProxySystemPtr proxy = manager->createProxy(system->getId());

	_modelFactory->createModels(romName, _models);

	for (auto& model : _models) {
		model.second->setSystem(system);
		model.second->onBeforeLoad(loadConfig);
	}

	system->load(std::forward<LoadConfig>(loadConfig));

	deserializeModels();

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

	_version++;

	return proxy;
}

void SystemWrapper::deserializeModels() {
	std::string& serialized = _settings.serialized;

	if (serialized.size()) {
		sol::state lua;
		SolUtil::prepareState(lua);
		sol::table modelTable;

		if (SolUtil::deserializeTable(lua, serialized, modelTable)) {
			for (auto& [type, model] : _models) {
				std::string_view typeName = MetaUtil::getTypeName(type);
				std::optional<sol::table> m = modelTable[typeName];

				if (m.has_value() && m.value().valid()) {
					spdlog::info("Deserializing data for {}", typeName);
					model->onDeserialize(lua, m.value());
				} else {
					spdlog::warn("Tried to deserialize {} but no data was found", typeName);
				}
			}
		}

		serialized.clear();
	}
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
