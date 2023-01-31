#pragma once
/*
#include <entt/meta/resolve.hpp>

#include "core/Model.h"
#include "core/System.h"
#include "core/SystemManager.h"
#include "core/SystemSettings.h"

namespace fw {
	class EventNode;
}

namespace rp {
	class SystemProcessor;
	class ModelFactory;

	class SystemWrapper {
	private:
		SystemId _systemId;
		SystemPtr _system;
		std::vector<std::pair<entt::id_type, ModelPtr>> _models;

		SystemProcessor* _processor;
		fw::EventNode* _eventNode;
		ModelFactory* _modelFactory;
		
		SystemDesc _desc;

		uint32 _version = 0;

	public:
		SystemWrapper(SystemId systemId, SystemProcessor* processor, fw::EventNode* eventNode, ModelFactory* modelFactory):
			_systemId(systemId), _processor(processor), _eventNode(eventNode), _modelFactory(modelFactory) {}
		~SystemWrapper() {} 

		SystemPtr load(const SystemDesc& systemDesc, LoadConfig&& loadConfig);

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

		void setGameLink(bool gameLink);

		SystemId getId() const {
			return _systemId;
		}

		SystemPtr getSystem() {
			return _system;
		}

		SystemDesc& getDesc() {
			return _desc;
		}

		const SystemDesc& getDesc() const {
			return _desc;
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

		bool saveSram() { return saveSram(_desc.paths.sramPath); }

		bool saveSram(std::string_view path);

	private:
		void deserializeModels();
	};

	using SystemPtr = std::shared_ptr<SystemWrapper>;
}
*/