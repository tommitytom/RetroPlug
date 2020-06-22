#pragma once

#include <vector>
#include <string>
#include <algorithm>
#include "util/DataBuffer.h"
#include "micromsg/node.h"
#include "controller/messaging.h"
#include "controller/AudioController.h"
#include "Constants.h"
#include "Types.h"
#include "model/Project.h"
#include "model/ButtonStream.h"
#include "model/FileManager.h"
#include "luawrapper/AudioLuaContext.h"
#include "plugs/SameBoyPlug.h"
#include "sol/sol.hpp"

const int MAX_STATE_SIZE = 512 * 1024;
const int MAX_SRAM_SIZE = 131072;

class AudioContextProxy {
private:
	Project _project;
	int _activeIdx = -1;

	Node* _node;

	FileManager _fileManager;

	std::string _configPath;
	std::string _scriptPath;

	AudioController* _audioController;

public:
	std::function<void(const VideoStream&)> videoCallback;

public:
	AudioContextProxy(AudioController* audioController): _audioController(audioController) { }
	~AudioContextProxy() {}

	FileManager* getFileManager() {
		return &_fileManager;
	}

	void updateSram(SystemIndex idx) {
		DataBuffer<char>* buffer = _project.systems[idx]->sourceSavData.get();
		_audioController->getSram(idx, buffer);
	}

	void setRenderingEnabled(bool enabled) {
		_node->push<calls::EnableRendering>(NodeTypes::Audio, enabled);
	}

	void prepareFetch(FetchStateRequest& req) {
		// TODO: Instead of using MAX_STATE_SIZE get the actual sram size from the emu
		for (size_t i = 0; i < MAX_SYSTEMS; ++i) {
			size_t type = (size_t)req.systems[i];
			
			if (type & (size_t)ResourceType::Sram) {
				req.srams[i] = std::make_shared<DataBuffer<char>>(MAX_SRAM_SIZE);
			}

			if (type & (size_t)ResourceType::State) {
				req.states[i] = std::make_shared<DataBuffer<char>>(MAX_STATE_SIZE);
			}
		}
	}

	void fetchResources(FetchStateRequest& req, std::function<void(const FetchStateResponse&)> cb) {
		prepareFetch(req);
		FetchStateResponse res;
		_audioController->getLock()->lock();
		_audioController->fetchState(req, res);
		_audioController->getLock()->unlock();
		cb(res);
	}

	void fetchResourcesAsync(FetchStateRequest& req, std::function<void(const FetchStateResponse&)> cb) {
		prepareFetch(req);
		_node->request<calls::FetchState>(NodeTypes::Audio, req, cb);
	}

	void fetchSystemStates(bool immediate, std::function<void(const FetchStateResponse&)> cb) {
		FetchStateRequest req;

		for (size_t i = 0; i < _project.systems.size(); ++i) {
			// TODO: Instead of using MAX_STATE_SIZE get the actual sram size from the emu
			req.srams[i] = std::make_shared<DataBuffer<char>>(MAX_SRAM_SIZE);
			req.states[i] = std::make_shared<DataBuffer<char>>(MAX_STATE_SIZE);
		}

		if (immediate) {
			FetchStateResponse res;
			_audioController->getLock()->lock();
			_audioController->fetchState(req, res);
			_audioController->getLock()->unlock();
			cb(res);
		} else {
			_node->request<calls::FetchState>(NodeTypes::Audio, req, cb);
		}
	}

	void setScriptDirs(const std::string& configPath, const std::string& scriptPath) {
		_configPath = configPath;
		_scriptPath = scriptPath;
		reloadLuaContext();
	}

	void setNode(Node* node) {
		_node = node;

		node->on<calls::TransmitVideo>([&](const VideoStream& buffer) {
			videoCallback(buffer);
		});
	}

	void updateSystemSettings(SystemIndex idx) {
		SystemSettings settings = SystemSettings{ idx, _project.systems[idx]->sameBoySettings };
		_node->push<calls::UpdateSystemSettings>(NodeTypes::Audio, settings);
	}

	void reloadLuaContext() {
		AudioLuaContextPtr ctx = std::make_shared<AudioLuaContext>(_configPath, _scriptPath);
		_node->request<calls::SwapLuaContext>(NodeTypes::Audio, ctx, [](const AudioLuaContextPtr& d) {});
	}

	Project* getProject() {
		return &_project;
	}

	void update(double delta) {
		try {
			_node->pull();
		} catch (sol::error error) {
			std::cout << error.what() << std::endl;
		}
	
		for (SystemDescPtr& system : _project.systems) {
			if (system->buttons.getCount() > 0) {
				if (_node->canPush<calls::PressButtons>()) {
					_node->push<calls::PressButtons>(NodeTypes::Audio, ButtonPressState{ 
						system->idx, 
						system->buttons.data() 
					});

					system->buttons.clear();
				} else {
					std::cout << "Unable to push buttons" << std::endl;
				}
			}
		}
	}

	void setRom(SystemIndex idx, DataBufferPtr romData, bool reset) {
		_node->request<calls::SetRom>(NodeTypes::Audio, SetDataRequest{ idx, romData, reset }, [](const DataBufferPtr&) {});
	}

	void setSram(SystemIndex idx, DataBufferPtr sramData, bool reset) {
		_node->request<calls::SetSram>(NodeTypes::Audio, SetDataRequest{ idx, sramData, reset }, [](const DataBufferPtr&) {});
	}

	void setState(SystemIndex idx, DataBufferPtr stateData, bool reset) {
		_node->request<calls::SetState>(NodeTypes::Audio, SetDataRequest{ idx, stateData, reset }, [](const DataBufferPtr&) {});
	}

	SystemState setSystem(SystemDescPtr& inst) {
		assert(inst->idx < MAX_SYSTEMS);

		if (inst->idx < _project.systems.size()) {
			_project.systems[inst->idx] = inst;
		} else if (inst->idx == _project.systems.size()) {
			_project.systems.push_back(inst);
		} else {
			assert(false);
			return SystemState::Uninitialized;
		}

		return loadRom(inst);
	}

	SystemState loadRom(SystemDescPtr& inst) {
		if (!inst->sourceRomData) {
			inst->state = SystemState::RomMissing;
			return SystemState::RomMissing;
		}

		SameBoyPlugPtr plug = std::make_shared<SameBoyPlug>();
		plug->setDesc({ inst->romName });
		plug->loadRom(inst->sourceRomData->data(), inst->sourceRomData->size(), inst->sameBoySettings, inst->fastBoot);

		if (inst->sourceStateData) {
			plug->loadState(inst->sourceStateData->data(), inst->sourceStateData->size());
		}

		if (inst->sourceSavData) {
			plug->loadBattery(inst->sourceSavData->data(), inst->sourceSavData->size(), false);
		} else {
			// TODO: Instead of using MAX_STATE_SIZE get the actual sram size from the emu
			inst->sourceSavData = std::make_shared<DataBuffer<char>>(MAX_SRAM_SIZE);
		}

		SystemSwapDesc swap = { inst->idx, plug, std::make_shared<std::string>(inst->audioComponentState) };
		_node->request<calls::SwapSystem>(NodeTypes::Audio, swap, [inst](const SystemSwapDesc& d) {
			inst->state = SystemState::Running;
		});

		inst->state = SystemState::Initialized;

		return SystemState::Initialized;
	}

	SystemState duplicateSystem(SystemIndex idx, SystemDescPtr& inst) {
		assert(_project.systems.size() < MAX_SYSTEMS);
		if (!inst->sourceRomData) {
			inst->state = SystemState::RomMissing;
			return SystemState::RomMissing;
		}

		inst->idx = _project.systems.size();
		inst->fastBoot = true;

		SameBoyPlugPtr plug = std::make_shared<SameBoyPlug>();
		plug->loadRom(inst->sourceRomData->data(), inst->sourceRomData->size(), inst->sameBoySettings, inst->fastBoot);
		plug->setDesc({ inst->romName });

		SystemDuplicateDesc swap = { (SystemIndex)idx, inst->idx, plug };
		_node->request<calls::DuplicateSystem>(NodeTypes::Audio, swap, [inst](const SameBoyPlugPtr& d) {
			inst->state = SystemState::Running;
		});

		_project.systems.push_back(inst);
		inst->state = SystemState::Initialized;

		return SystemState::Initialized;
	}

	void resetSystem(SystemIndex idx, GameboyModel model) {
		_node->push<calls::ResetSystem>(NodeTypes::Audio, ResetSystemDesc { idx, model });
	}

	void removeSystem(SystemIndex idx) {
		_node->request<calls::TakeSystem>(NodeTypes::Audio, idx, [](const SameBoyPlugPtr&) {});
		_project.systems.erase(_project.systems.begin() + idx);

		for (SystemIndex i = idx; i < (SystemIndex)_project.systems.size(); ++i) {
			_project.systems[i]->idx = i;
		}

		if (_project.selectedSystem == idx && _project.systems.size() > 0) {
			_project.selectedSystem = std::min(idx, (int)(_project.systems.size() - 1));
		}
	}

	void clearProject() {
		for (int i = (int)_project.systems.size() - 1; i >= 0; --i) {
			removeSystem(i);
		}

		_project = Project();
	}

	void updateSettings() {
		_node->push<calls::UpdateProjectSettings>(NodeTypes::Audio, _project.settings);
	}

	std::vector<Menu*> onMenu(SystemIndex idx) {
		std::vector<Menu*> items;
		_audioController->onMenu(idx, items);
		return items;
	}

	void onMenuResult(int idx) {
		_node->push<calls::ContextMenuResult>(NodeTypes::Audio, idx);
	}
};
