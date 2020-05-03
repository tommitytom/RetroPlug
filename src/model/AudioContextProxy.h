#pragma once

#include <vector>
#include <string>
#include "util/DataBuffer.h"
#include "micromsg/node.h"
#include "controller/messaging.h"
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

public:
	std::function<void(const VideoStream&)> videoCallback;

public:
	AudioContextProxy() { }
	~AudioContextProxy() {}

	EmulatorInstanceState setSystem(EmulatorInstanceDescPtr& inst) {
		assert(inst->idx < MAX_INSTANCES);

		if (inst->idx < _project.instances.size()) {
			_project.instances[inst->idx] = inst;
		} else if (inst->idx == _project.instances.size()) {
			_project.instances.push_back(inst);
		} else {
			assert(false);
			return EmulatorInstanceState::Uninitialized;
		}

		return loadRom(inst);
	}

	EmulatorInstanceState loadRom(EmulatorInstanceDescPtr& inst) {
		if (!inst->sourceRomData) {
			inst->state = EmulatorInstanceState::RomMissing;
			return EmulatorInstanceState::RomMissing;
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

		InstanceSwapDesc swap = { inst->idx, plug, std::make_shared<std::string>(inst->audioComponentState) };
		_node->request<calls::SwapInstance>(NodeTypes::Audio, swap, [inst](const InstanceSwapDesc& d) {
			inst->state = EmulatorInstanceState::Running;
		});

		inst->state = EmulatorInstanceState::Initialized;

		return EmulatorInstanceState::Initialized;
	}

	EmulatorInstanceDescPtr duplicateSystem(InstanceIndex idx, EmulatorInstanceDescPtr& inst) {
		assert(_project.instances.size() < MAX_INSTANCES);

		EmulatorInstanceDescPtr instance = std::make_shared<EmulatorInstanceDesc>();
		*instance = *_project.instances[idx];
		_project.instances.push_back(instance);

		instance->idx = getInstanceCount() - 1;
		instance->fastBoot = true;

		SameBoyPlugPtr plug = std::make_shared<SameBoyPlug>();
		if (instance->patchedRomData) {
			plug->loadRom(instance->patchedRomData->data(), instance->patchedRomData->size(), instance->sameBoySettings.model, instance->fastBoot);
		} else {
			assert(instance->sourceRomData);
			plug->loadRom(instance->sourceRomData->data(), instance->sourceRomData->size(), instance->sameBoySettings.model, instance->fastBoot);
		}

		plug->setDesc({ instance->romName });

		InstanceDuplicateDesc swap = { (InstanceIndex)idx, instance->idx, plug };
		_node->request<calls::DuplicateInstance>(NodeTypes::Audio, swap, [instance](const SameBoyPlugPtr& d) {
			instance->state = EmulatorInstanceState::Running;
		});

		return instance;
	}
};
