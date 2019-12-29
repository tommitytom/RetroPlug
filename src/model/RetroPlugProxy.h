#pragma once

#include <vector>
#include <string>
#include "util/DataBuffer.h"
#include "micromsg/node.h"
#include "controller/messaging.h"
#include "Constants.h"
#include "Types.h"
#include "model/Project.h"

class RetroPlugProxy {
private:
	Project _project;
	int _activeIdx = -1;

	Node* _node;

	FileManager _fileManager;

public:
	std::function<void(const VideoStream&)> videoCallback;

public:
	RetroPlugProxy() {
		for (size_t i = 0; i < MAX_INSTANCES; i++) {
			_project.instances.push_back(EmulatorInstanceDesc());
		}

		_project.instances[0].type = EmulatorType::Placeholder;

		updateIndices();
	}

	~RetroPlugProxy() {}

	void setNode(Node* node) { 
		_node = node; 

		node->on<calls::TransmitVideo>([&](const VideoStream& buffer) {
			videoCallback(buffer);
		});
	}

	FileManager* fileManager() { return &_fileManager; }

	int activeIdx() const { return _activeIdx; }

	Project* getProject() { return &_project; }

	void update(double delta) {
		_node->pull();
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
			plug->loadRom(t.patchedRomData->data(), t.patchedRomData->size());
		} else {
			assert(t.sourceRomData);
			plug->loadRom(t.sourceRomData->data(), t.sourceRomData->size());
		}
		
		if (t.sourceStateData) {
			//plug->loadState(t.sourceStateData->data(), t.sourceStateData->size());
		}

		if (t.patchedSavData) {
			//plug->loadBattery(t.patchedSavData->data(), t.patchedSavData->size());
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

		_node->request<calls::TakeInstance>(NodeTypes::Audio, idx, [&](const SameBoyPlugPtr& d) {});
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
