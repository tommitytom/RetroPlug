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

class RetroPlugProxy {
private:
	Project _project;
	int _activeIdx = -1;

	Node* _node;

	FileManager _fileManager;

	GameboyButtonStream _buttonPresses[MAX_INSTANCES];

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
		for (size_t i = 0; i < MAX_INSTANCES; i++) {
			_project.instances.push_back(EmulatorInstanceDesc());
			_buttonPresses[i].setIndex(i);
		}

		_project.instances[0].emulatorType = EmulatorType::Placeholder;
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
		size_t count = getInstanceCount();
		for (size_t i = 0; i < count; ++i) {
			req.buffers[i] = std::make_shared<DataBuffer<char>>(bufferSize);
		}

		_node->request<calls::FetchState>(NodeTypes::Audio, req, cb);
	}

	void update(double delta) {
		_node->pull();

		for (size_t i = 0; i < MAX_INSTANCES; ++i) {
			if (_buttonPresses[i].getCount() > 0) {
				_node->push<calls::PressButtons>(NodeTypes::Audio, _buttonPresses[i].data());
				_buttonPresses[i].clear();
			}
		}
	}

	size_t getInstanceCount() const {
		size_t count = 0;
		for (size_t i = 0; i < _project.instances.size(); ++i) {
			if (_project.instances[i].state != EmulatorInstanceState::Uninitialized) {
				count++;
			}
		}

		return count;
	}

	EmulatorInstanceDesc* getActiveInstance() {
		if (_activeIdx != NO_ACTIVE_INSTANCE) {
			return &_project.instances[_activeIdx];
		}
		
		return nullptr;
	}

	void setActive(int idx) {
		_activeIdx = idx;
	}

	void updateSettings() {
		_node->push<calls::UpdateSettings>(NodeTypes::Audio, _project.settings);
	}

	void setInstance(const EmulatorInstanceDesc& instance) {
		assert(instance.idx < MAX_INSTANCES);
		EmulatorInstanceDesc& t = _project.instances[instance.idx];
		t = instance;
		t.state = EmulatorInstanceState::Initialized;

		SameBoyPlugPtr plug = std::make_shared<SameBoyPlug>();
		if (t.patchedRomData) {
			plug->loadRom(t.patchedRomData->data(), t.patchedRomData->size(), instance.fastBoot);
		} else {
			assert(t.sourceRomData);
			plug->loadRom(t.sourceRomData->data(), t.sourceRomData->size(), instance.fastBoot);
		}
		
		if (t.sourceStateData) {
			plug->loadState(t.sourceStateData->data(), t.sourceStateData->size());
		}

		if (t.patchedSavData) {
			plug->loadBattery(t.patchedSavData->data(), t.patchedSavData->size(), false);
		} else if (t.sourceSavData) {
			plug->loadBattery(t.sourceSavData->data(), t.sourceSavData->size(), false);
		}

		InstanceSwapDesc swap = { t.idx, plug };
		_node->request<calls::SwapInstance>(NodeTypes::Audio, swap, [&](const SameBoyPlugPtr& d) {
			t.state = EmulatorInstanceState::Running;
		});
	}

	void removeInstance(size_t idx) {
		_project.instances.erase(_project.instances.begin() + idx);
		_project.instances.push_back(EmulatorInstanceDesc());
		updateIndices();

		_node->request<calls::TakeInstance>(NodeTypes::Audio, idx, [](const SameBoyPlugPtr& d) {});
	}

	void closeProject() {
		while (_project.instances[0].state != EmulatorInstanceState::Uninitialized) {
			removeInstance(0);
		}

		initProject();
		_activeIdx = NO_ACTIVE_INSTANCE;
	}

	const EmulatorInstanceDesc* getInstance(InstanceIndex idx) const {
		return &_project.instances[idx];
	}

	const std::vector<EmulatorInstanceDesc>& instances() const {
		return _project.instances;
	}

private:
	void updateIndices() {
		for (size_t i = 0; i < MAX_INSTANCES; i++) {
			_project.instances[i].idx = i;
		}
	}
};
