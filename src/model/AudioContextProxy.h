#pragma once

#include <vector>
#include <string>
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

	const Project& getProject() const {
		return _project;
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

	SystemDescPtr duplicateSystem(SystemIndex idx, SystemDescPtr& inst) {
		assert(_project.systems.size() < MAX_SYSTEMS);

		SystemDescPtr instance = std::make_shared<SystemDesc>();
		*instance = *_project.systems[idx];

		instance->idx = _project.systems.size();
		instance->fastBoot = true;

		_project.systems.push_back(instance);

		SameBoyPlugPtr plug = std::make_shared<SameBoyPlug>();
		if (instance->patchedRomData) {
			plug->loadRom(instance->patchedRomData->data(), instance->patchedRomData->size(), instance->sameBoySettings.model, instance->fastBoot);
		} else {
			assert(instance->sourceRomData);
			plug->loadRom(instance->sourceRomData->data(), instance->sourceRomData->size(), instance->sameBoySettings.model, instance->fastBoot);
		}

		plug->setDesc({ instance->romName });

		SystemDuplicateDesc swap = { (SystemIndex)idx, instance->idx, plug };
		_node->request<calls::DuplicateSystem>(NodeTypes::Audio, swap, [instance](const SameBoyPlugPtr& d) {
			instance->state = SystemState::Running;
		});

		return instance;
	}
};
