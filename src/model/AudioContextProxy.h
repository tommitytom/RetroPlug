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
#include "model/AudioLuaContext.h"
#include "plugs/SameBoyPlug.h"

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

	void reloadLuaContext() {
		AudioLuaContextPtr ctx = std::make_shared<AudioLuaContext>(_configPath, _scriptPath);
		_node->request<calls::SwapLuaContext>(NodeTypes::Audio, ctx, [](const AudioLuaContextPtr& d) {});
	}

	Project* getProject() {
		return &_project;
	}

	void update(double delta) {
		_node->pull();

		for (SystemDescPtr& system : _project.systems) {
			if (system->buttons.getCount() > 0) {
				_node->push<calls::PressButtons>(NodeTypes::Audio, system->buttons.data());
			}
		}
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
		plug->loadRom(inst->sourceRomData->data(), inst->sourceRomData->size(), inst->sameBoySettings.model, inst->fastBoot);

		if (inst->sourceStateData) {
			plug->loadState(inst->sourceStateData->data(), inst->sourceStateData->size());
		}

		if (inst->sourceSavData) {
			plug->loadBattery(inst->sourceSavData->data(), inst->sourceSavData->size(), false);
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
		plug->loadRom(inst->sourceRomData->data(), inst->sourceRomData->size(), inst->sameBoySettings.model, inst->fastBoot);
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
		_node->push<calls::UpdateSettings>(NodeTypes::Audio, _project.settings);
	}
};
