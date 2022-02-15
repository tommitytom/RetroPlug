#pragma once

#include "core/Model.h"
#include "core/System.h"
#include "core/SystemManager.h"

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

	public:
		SystemWrapper(SystemId systemId, SystemProcessor* processor, OrchestratorMessageBus* messageBus, ModelFactory* modelFactory): 
			_systemId(systemId), _processor(processor), _messageBus(messageBus), _modelFactory(modelFactory) {}
		~SystemWrapper() {} 

		SystemPtr load(LoadConfig&& loadConfig);

		template <typename T>
		std::shared_ptr<T> getModel() {
			entt::id_type typeId = entt::type_id<T>().seq();

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
	};

	using SystemWrapperPtr = std::shared_ptr<SystemWrapper>;
}
