#pragma once

#include <boost/filesystem.hpp>
#include <string>
#include <algorithm>

#include "plugs/SameBoyPlug.h"
#include "util/xstring.h"
#include "Constants.h"

enum class InstanceLayout {
	Auto,
	Row,
	Column,
	Grid
};

enum class EmulatorType {
	SameBoy
};

enum class AudioChannelRouting {
	StereoMixDown,
	TwoChannelsPerInstance,
	TwoChannelsPerChannel
};

enum class MidiChannelRouting {
	SendToAll,
	FourChannelsPerInstance,
	OneChannelPerInstance
};

class RetroPlug {
private:
	SameBoyPlugPtr _plugs[MAX_INSTANCES];
	std::wstring _projectPath;
	InstanceLayout _layout = InstanceLayout::Auto;
	SaveStateType _saveType = SaveStateType::State;

	std::atomic<AudioChannelRouting> _audioRouting = AudioChannelRouting::StereoMixDown;
	std::atomic<MidiChannelRouting> _midiRouting = MidiChannelRouting::SendToAll;

	double _sampleRate = 48000;
public:
	RetroPlug() {}
	~RetroPlug() {}

	InstanceLayout layout() const { return _layout; }

	void setLayout(InstanceLayout layout) { _layout = layout; }

	AudioChannelRouting audioRouting() const { return _audioRouting; }

	void setAudioRouting(AudioChannelRouting mode) { _audioRouting = mode; }

	MidiChannelRouting midiRouting() const { return _midiRouting; }

	void setMidiRouting(MidiChannelRouting mode) { _midiRouting = mode; }

	SaveStateType saveType() const { return _saveType; }

	void setSaveType(SaveStateType type) { _saveType = type; }

	void clear() {
		_projectPath.clear();
		for (size_t i = 0; i < MAX_INSTANCES; i++) {
			_plugs[i] = nullptr;
		}
	}

	const std::wstring& projectPath() const {
		return _projectPath;
	}

	void setProjectPath(const std::wstring& path) {
		_projectPath = path;
	}

	SameBoyPlugPtr addInstance(EmulatorType emulatorType) {
		SameBoyPlugPtr plug = std::make_shared<SameBoyPlug>();
		plug->setSampleRate(_sampleRate);

		for (size_t i = 0; i < MAX_INSTANCES; i++) {
			if (!_plugs[i]) {
				_plugs[i] = plug;
				break;
			}
		}

		return plug;
	}

	void removeInstance(size_t idx) {
		for (size_t i = idx; i < MAX_INSTANCES - 1; i++) {
			_plugs[i] = _plugs[i + 1];
		}

		_plugs[MAX_INSTANCES - 1] = nullptr;
	}

	size_t instanceCount() const {
		for (size_t i = 0; i < MAX_INSTANCES; i++) {
			if (!_plugs[i]) {
				return i;
			}
		}

		return 4;
	}

	void getLinkTargets(std::vector<SameBoyPlugPtr>& targets, SameBoyPlugPtr ignore) {
		for (size_t i = 0; i < MAX_INSTANCES; i++) {
			if (_plugs[i] && _plugs[i] != ignore && _plugs[i]->active() && _plugs[i]->gameLink()) {
				targets.push_back(_plugs[i]);
			}
		}
	}

	void updateLinkTargets() {
		size_t count = instanceCount();
		std::vector<SameBoyPlugPtr> targets;
		for (size_t i = 0; i < count; i++) {
			auto target = _plugs[i];
			if (target->active() && target->gameLink()) {
				targets.clear();
				getLinkTargets(targets, target);
				target->setLinkTargets(targets);
			}
		}
	}

	void setSampleRate(double sampleRate) {
		_sampleRate = sampleRate;

		for (size_t i = 0; i < MAX_INSTANCES; i++) {
			SameBoyPlugPtr plugPtr = _plugs[i];
			if (plugPtr) {
				plugPtr->setSampleRate(sampleRate);
			}
		}
	}

	SameBoyPlugPtr getPlug(size_t idx) {
		return _plugs[idx];
	}

	SameBoyPlugPtr* plugs() {
		return _plugs;
	}

	const SameBoyPlugPtr* plugs() const {
		return _plugs;
	}
};
