#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <sol/forward.hpp>

#include "core/ModelFactory.h"
#include "core/ProjectState.h"
#include "core/ResourceManager.h"
#include "core/Serializable.h"
#include "core/SystemWrapper.h"

namespace rp {
	class AudioManager;

	class Project {
	private:
		ProjectState _state;
		ResourceManager _resourceManager;
		AudioManager* _audioManager = nullptr;

		sol::state* _lua = nullptr;
		int32 _version = 0;
		SystemId _nextId = 1;

		std::vector<SystemWrapperPtr> _systems;

		SystemProcessor* _processor = nullptr;
		OrchestratorMessageBus* _messageBus = nullptr;
		ModelFactory _modelFactory;

		bool _copyLocal = true;
		bool _requiresSave = false;

	public:
		Project();
		~Project();

		ModelFactory& getModelFactory() {
			return _modelFactory;
		}

		void setup(SystemProcessor* processor, OrchestratorMessageBus* orchestratorMessageBus) {
			_processor = processor;
			_messageBus = orchestratorMessageBus;
		}

		void setAudioManager(AudioManager& audioManager) {
			_audioManager = &audioManager;
		}

		AudioManager& getAudioManager() {
			return *_audioManager;
		}

		SystemProcessor& getProcessor() {
			return *_processor;
		}

		void processIncoming();

		/*template <typename T>
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
		}*/

		void update(f32 delta) {
			for (SystemWrapperPtr system : _systems) {
				system->update(delta);
			}
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

		void saveIfRequired() {
			if (!_state.path.empty()) {
				for (SystemWrapperPtr& system : _systems) {
					for (auto [type, model] : system->getModels()) {
						if (model->getRequiresSave()) {
							_requiresSave = true;
							model->setRequiresSave(false);
						}
					}
				}

				if (_requiresSave && _state.settings.autoSave) {
					save();
					_requiresSave = false;
				}
			}
			
		}

		template <typename T>
		SystemWrapperPtr addSystem(const SystemSettings& settings, SystemId systemId = INVALID_SYSTEM_ID) {
			return addSystem(entt::type_id<T>().seq(), settings);
		}

		SystemWrapperPtr addSystem(SystemType type, const SystemSettings& settings, SystemId systemId = INVALID_SYSTEM_ID);

		template <typename T>
		SystemWrapperPtr addSystem(const SystemSettings& settings, LoadConfig&& loadConfig, SystemId systemId = INVALID_SYSTEM_ID) {
			return addSystem(entt::type_id<T>().seq(), settings, std::forward<LoadConfig>(loadConfig));
		}

		SystemWrapperPtr addSystem(SystemType type, const SystemSettings& settings, LoadConfig&& loadConfig, SystemId systemId = INVALID_SYSTEM_ID);

		void removeSystem(SystemId systemId);

		void duplicateSystem(SystemId systemId, const SystemSettings& settings);

		SystemWrapperPtr findSystem(SystemId systemId);

		void clear();

		int32 getVersion() const {
			return _version;
		}

		f32 getScale() const {
			return (f32)_state.settings.zoom + 1.0f;
		}

		std::vector<SystemWrapperPtr>& getSystems() {
			return _systems;
		}

		

	private:
		void deserializeModel(SystemId systemId, SystemWrapperPtr system, std::shared_ptr<Model> model);
	};
}
