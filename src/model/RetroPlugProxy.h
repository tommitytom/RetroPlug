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

class RetroPlugProxy {
private:
	Project _project;
	int _activeIdx = -1;

	Node* _node;

	FileManager _fileManager;

	GameboyButtonStream _buttonPresses[MAX_INSTANCES];

	std::string _configPath;
	std::string _scriptPath;

public:
	std::function<void(const VideoStream&)> videoCallback;

public:
	RetroPlugProxy() {
		initProject();
		updateIndices();
	}

	~RetroPlugProxy() {}

	void initProject() {
		_project = Project();
		
		EmulatorInstanceDescPtr root = std::make_shared<EmulatorInstanceDesc>();
		//root->emulatorType = EmulatorType::Placeholder;
		//_project.instances.push_back(root);

		for (size_t i = 0; i < MAX_INSTANCES; i++) {
			_buttonPresses[i].setIndex(i);
		}
	}

	void setNode(Node* node) { 
		_node = node; 

		node->on<calls::TransmitVideo>([&](const VideoStream& buffer) {
			videoCallback(buffer);
		});
	}

	GameboyButtonStream* getButtonPresses(InstanceIndex idx) {
		return &_buttonPresses[idx];
	}

	FileManager* fileManager() { 
		return &_fileManager; 
	}

	int activeIdx() const { 
		return _activeIdx; 
	}

	Project* getProject() { 
		return &_project; 
	}

	void requestSave(std::function<void(const FetchStateResponse& res)> cb) {
		assert(!_project.path.empty());

		const size_t SRAM_SIZE = 128 * 1024;
		const size_t STATE_SIZE = 1024 * 1024;

		size_t bufferSize = _project.settings.saveType == SaveStateType::Sram ? SRAM_SIZE : STATE_SIZE;

		FetchStateRequest req;
		req.type = _project.settings.saveType;
		for (size_t i = 0; i < getInstanceCount(); ++i) {
			req.buffers[i] = std::make_shared<DataBuffer<char>>(bufferSize);
		}

		_node->request<calls::FetchState>(NodeTypes::Audio, req, cb);
	}

	void update(double delta) {
		_node->pull();

		for (size_t i = 0; i < getInstanceCount(); ++i) {
			if (_buttonPresses[i].getCount() > 0) {
				_node->push<calls::PressButtons>(NodeTypes::Audio, _buttonPresses[i].data());
				_buttonPresses[i].clear();
			}
		}
	}

	void setActive(InstanceIndex idx) {
		_activeIdx = idx;
		_node->push<calls::SetActive>(NodeTypes::Audio, idx);
	}

	void onMenuResult(int id) {
		_node->push<calls::ContextMenuResult>(NodeTypes::Audio, id);
	}

	void updateSettings() {
		_node->push<calls::UpdateSettings>(NodeTypes::Audio, _project.settings);
	}

	void setSram(InstanceIndex idx, DataBuffer<char>* data, bool reset) {
		SetSramRequest req;
		req.idx = idx;
		req.buffer = std::make_shared<DataBuffer<char>>(data->size());
		req.buffer->write(data->data(), data->size());
		req.reset = reset;

		_node->request<calls::SetSram>(NodeTypes::Audio, req, [](const DataBufferPtr&) {});
	}

	void setScriptDirs(const std::string& configPath, const std::string& scriptPath) {
		_configPath = configPath;
		_scriptPath = scriptPath;
		reloadLuaContext();
	}

	void reloadLuaContext() {
		AudioLuaContextPtr ctx = std::make_shared<AudioLuaContext>(_configPath, _scriptPath);
		_node->request<calls::SwapLuaContext>(NodeTypes::Audio, ctx, [](const AudioLuaContextPtr& d) {});
	}

	EmulatorInstanceDescPtr duplicateInstance(size_t idx) {
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

	EmulatorInstanceDescPtr createInstance() const {
		return std::make_shared<EmulatorInstanceDesc>();
	}

	void setInstance(EmulatorInstanceDescPtr inst) {
		assert(inst->idx < MAX_INSTANCES);
		inst->state = EmulatorInstanceState::Initialized;

		if (inst->idx < _project.instances.size()) {
			_project.instances[inst->idx] = inst;
		} else if (inst->idx == _project.instances.size()) {
			_project.instances.push_back(inst);
		} else {
			assert(false);
			return;
		}

		SameBoyPlugPtr plug = std::make_shared<SameBoyPlug>();
		if (inst->patchedRomData) {
			plug->loadRom(inst->patchedRomData->data(), inst->patchedRomData->size(), inst->sameBoySettings.model, inst->fastBoot);
		} else {
			assert(inst->sourceRomData);
			plug->loadRom(inst->sourceRomData->data(), inst->sourceRomData->size(), inst->sameBoySettings.model, inst->fastBoot);
		}
		
		if (inst->sourceStateData) {
			plug->loadState(inst->sourceStateData->data(), inst->sourceStateData->size());
		}

		if (inst->patchedSavData) {
			plug->loadBattery(inst->patchedSavData->data(), inst->patchedSavData->size(), false);
		} else if (inst->sourceSavData) {
			plug->loadBattery(inst->sourceSavData->data(), inst->sourceSavData->size(), false);
		}

		plug->setDesc({ inst->romName });

		InstanceSwapDesc swap = { inst->idx, plug };
		_node->request<calls::SwapInstance>(NodeTypes::Audio, swap, [inst](const SameBoyPlugPtr& d) {
			inst->state = EmulatorInstanceState::Running;
		});
	}

	void removeInstance(InstanceIndex idx) {
		assert(idx != NO_ACTIVE_INSTANCE);
		_project.instances.erase(_project.instances.begin() + idx);
		updateIndices();

		_node->request<calls::TakeInstance>(NodeTypes::Audio, idx, [](const SameBoyPlugPtr& d) {});
	}

	void resetInstance(InstanceIndex idx, GameboyModel model) {
		ResetInstanceDesc desc = { idx, model };
		_node->push<calls::ResetInstance>(NodeTypes::Audio, desc);
	}

	void closeProject() {
		for (int i = _project.instances.size() - 1; i >= 0; i--) {
			removeInstance(i);
		}

		initProject();
		_activeIdx = NO_ACTIVE_INSTANCE;
	}

	const EmulatorInstanceDescPtr getInstance(InstanceIndex idx) const {
		return _project.instances[idx];
	}

	const std::vector<EmulatorInstanceDescPtr>& instances() const {
		return _project.instances;
	}

	size_t getInstanceCount() const {
		return _project.instances.size();
	}

	EmulatorInstanceDescPtr getActiveInstance() {
		if (_activeIdx != NO_ACTIVE_INSTANCE) {
			return _project.instances[_activeIdx];
		}

		return nullptr;
	}

private:
	void updateIndices() {
		for (size_t i = 0; i < _project.instances.size(); i++) {
			_project.instances[i]->idx = i;
		}
	}
};
