#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <sol/forward.hpp>

#include "core/Events.h"
#include "core/ModelFactory.h"
#include "core/ProjectState.h"
#include "core/Serializable.h"
#include "core/System.h"
#include "core/SystemManager.h"
#include "audio/AudioManager.h"

namespace rp {
	class Project {
	private:
		GlobalConfig _config;
		ProjectState _state;

		const fw::TypeRegistry& _typeRegistry;
		const SystemFactory& _systemFactory;
		ConcurrentPoolAllocator<SystemIo>& _ioAllocator;
		SystemManager _systemManager;

		sol::state* _lua = nullptr;
		int32 _version = 0;
		SystemId _nextId = 1;

		fw::EventNode* _eventNode = nullptr;
		ModelFactory _modelFactory;

		bool _copyLocal = true;
		bool _requiresSave = false;

	public:
		Project(const fw::TypeRegistry& typeRegistry, const SystemFactory& systemFactory, ConcurrentPoolAllocator<SystemIo>& ioAllocator);
		~Project();

		ModelFactory& getModelFactory() {
			return _modelFactory;
		}

		const SystemFactory& getSystemFactory() const {
			return _systemFactory;
		}

		void setup(fw::EventNode& eventNode, FetchStateResponse&& state);

		std::string getName();

		void update(uint32 audioFrameCount) {
			// Make sure systems have IO buffers
			for (SystemPtr& system : _systemManager.getSystems()) {
				if (!system->hasIo()) {
					SystemIoPtr io = _ioAllocator.allocate();
					io->reset();

					system->setIo(std::move(io));
				}
			}

			_systemManager.process(audioFrameCount);
		}

		const ProjectState& getState() const {
			return _state;
		}

		ProjectState& getState() {
			return _state;
		}

		bool load(std::string_view path);

		bool save();

		bool save(std::string_view path) {
			_state.path = std::string(path);
			return save();
		}

		void saveIfRequired() {
			if (!_state.path.empty()) {
				/*for (SystemPtr& system : _systems) {
					for (auto [type, model] : system->getModels()) {
						if (model->getRequiresSave()) {
							_requiresSave = true;
							model->setRequiresSave(false);
						}
					}
				}*/	

				if (_requiresSave && _state.settings.autoSave) {
					save();
					_requiresSave = false;
				}
			}		
		}

		SystemPtr addSystem(SystemType type, const SystemDesc& systemDesc, SystemId systemId = INVALID_SYSTEM_ID);

		SystemPtr addSystem(SystemType type, LoadConfig&& loadConfig, SystemId systemId = INVALID_SYSTEM_ID);

		void removeSystem(SystemId systemId);

		SystemPtr duplicateSystem(SystemId systemId = INVALID_SYSTEM_ID);

		void clear();

		int32 getVersion() const {
			return _version;
		}

		f32 getScale() const {
			return (f32)_state.settings.zoom + 1.0f;
		}

		std::vector<SystemPtr>& getSystems() {
			return _systemManager.getSystems();
		}

		SystemManager& getSystemManager() {
			return _systemManager;
		}

		const GlobalConfig& getGlobalConfig() const {
			return _config;
		}
	};
}
