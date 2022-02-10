#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <sol/forward.hpp>

#include "core/ProjectState.h"
#include "core/ResourceManager.h"
#include "core/SystemOrchestrator.h"
#include "core/Serializable.h"

namespace rp {
	class Project {
	private:
		ProjectState _state;
		ResourceManager _resourceManager;

		SystemOrchestrator* _orchestrator = nullptr;

		sol::state* _lua = nullptr;
		int32 _version = 0;

	public:
		Project();
		~Project();

		template <typename T>
		std::shared_ptr<T> addModel(SystemId systemId) {
			entt::id_type typeId = entt::type_id<T>().seq();
			std::shared_ptr<T> v = std::make_shared<T>();

			for (SystemPtr system : _orchestrator->getSystems()) {
				if (system->getId() == systemId) {
					v->setSystem(system);
					break;
				}				
			}			

			deserializeModel(systemId, v);
			_state.systemSettings[systemId].models[typeId] = v;

			return v;
		}

		template <typename T>
		std::shared_ptr<T> getModel(SystemId systemId) {
			entt::id_type typeId = entt::type_id<T>().seq();
			SystemSettings& systemSettings = _state.systemSettings[systemId];

			auto found = systemSettings.models.find(typeId);
			if (found != systemSettings.models.end()) {
				return std::static_pointer_cast<T>(found->second);
			}

			return nullptr;
		}

		template <typename T>
		std::shared_ptr<T> getOrCreateModel(SystemId systemId) {
			std::shared_ptr<T> state = getModel<T>(systemId);
			if (state) {
				return state;
			}

			return addModel<T>(systemId);
		}

		const ProjectState& getState() const {
			return _state;
		}

		ProjectState& getState() {
			return _state;
		}

		void load(std::string_view path);

		bool save() { return save(_state.path); }

		bool save(std::string_view path);

		template <typename T>
		SystemPtr addSystem(std::string_view romPath, std::string_view sramPath = "") {
			return addSystem(entt::type_id<T>().seq(), romPath, sramPath);
		}

		SystemPtr addSystem(SystemType type, std::string_view romPath, std::string_view sramPath = "");

		template <typename T>
		SystemPtr addSystem(LoadConfig&& loadConfig) {
			return addSystem(entt::type_id<T>().seq(), std::forward<LoadConfig>(loadConfig));
		}

		SystemPtr addSystem(SystemType type, LoadConfig&& loadConfig);

		void removeSystem(SystemId systemId);

		void duplicateSystem(SystemId systemId);

		void clear();

		int32 getVersion() const {
			return _version;
		}

		f32 getScale() const {
			return (f32)_state.settings.zoom + 1.0f;
		}

		void setOrchestrator(SystemOrchestrator* orchestrator) {
			_orchestrator = orchestrator;
		}

		SystemOrchestrator* getOrchestrator() {
			return _orchestrator;
		}


	private:
		void deserializeModel(SystemId systemId, std::shared_ptr<Model> model);
	};
}
