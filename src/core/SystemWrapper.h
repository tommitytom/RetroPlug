#pragma once

#include <entt/meta/resolve.hpp>

#include "core/Model.h"
#include "core/System.h"
#include "core/SystemManager.h"
#include "core/SystemSettings.h"

namespace rp {
	class SystemProcessor;
	struct OrchestratorMessageBus;
	class ModelFactory;

	class SystemWrapper {
	private:
		SystemId _systemId;
		SystemPtr _system;
		std::vector<std::pair<entt::id_type, ModelPtr>> _models;

		SystemProcessor* _processor;
		OrchestratorMessageBus* _messageBus;
		ModelFactory* _modelFactory;

		SystemSettings _settings;

		uint32 _version = 0;

	public:
		SystemWrapper(SystemId systemId, SystemProcessor* processor, OrchestratorMessageBus* messageBus, ModelFactory* modelFactory): 
			_systemId(systemId), _processor(processor), _messageBus(messageBus), _modelFactory(modelFactory) {}
		~SystemWrapper() {} 

		SystemPtr load(const SystemSettings& settings, LoadConfig&& loadConfig);

		template <typename T>
		std::shared_ptr<T> getModel() {
			entt::id_type typeId = entt::resolve<T>().id();

			for (auto& model : _models) {
				if (model.first == typeId) {
					return std::static_pointer_cast<T>(model.second);
				}
			}

			return nullptr;
		}

		void reset();

		SystemId getId() const {
			return _systemId;
		}

		SystemPtr getSystem() {
			return _system;
		}

		const SystemSettings& getSettings() const {
			return _settings;
		}

		std::vector<std::pair<entt::id_type, ModelPtr>>& getModels() {
			return _models;
		}

		const std::vector<std::pair<entt::id_type, ModelPtr>>& getModels() const {
			return _models;
		}

		uint32 getVersion() const {
			return _version;
		}

		void update(f32 delta) {
			for (auto& system : _models) {
				system.second->onUpdate(delta);
			}
		}

		bool saveSram() { return saveSram(_settings.sramPath); }

		bool saveSram(std::string_view path);
	};

	using SystemWrapperPtr = std::shared_ptr<SystemWrapper>;
}
