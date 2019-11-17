#pragma once

#include <string>
#include <algorithm>

#include "plugs/SameBoyPlug.h"
#include "util/xstring.h"
#include "util/fs.h"
#include "Constants.h"

#include "IGraphicsStructs.h"

namespace sol {
	class state;
};

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
	SameBoyPlugPtr _active;
	tstring _projectPath;
	InstanceLayout _layout = InstanceLayout::Auto;
	SaveStateType _saveType = SaveStateType::State;

	std::atomic<AudioChannelRouting> _audioRouting = AudioChannelRouting::StereoMixDown;
	std::atomic<MidiChannelRouting> _midiRouting = MidiChannelRouting::SendToAll;

	double _sampleRate = 48000;

	sol::state* _lua;

public:
	RetroPlug();
	~RetroPlug();

	void setActive(SameBoyPlugPtr active);

	void onKey(const iplug::igraphics::IKeyPress& key, bool down);

	void onPad(int button, bool down);

	InstanceLayout layout() const { return _layout; }

	void setLayout(InstanceLayout layout) { _layout = layout; }

	AudioChannelRouting audioRouting() const { return _audioRouting; }

	void setAudioRouting(AudioChannelRouting mode) { _audioRouting = mode; }

	MidiChannelRouting midiRouting() const { return _midiRouting; }

	void setMidiRouting(MidiChannelRouting mode) { _midiRouting = mode; }

	SaveStateType saveType() const { return _saveType; }

	void setSaveType(SaveStateType type) { _saveType = type; }

	void clear();

	const tstring& projectPath() const { return _projectPath; }

	void setProjectPath(const tstring& path) { _projectPath = path; }

	SameBoyPlugPtr addInstance(EmulatorType emulatorType);

	void removeInstance(size_t idx);

	size_t instanceCount() const;

	void getLinkTargets(std::vector<SameBoyPlugPtr>& targets, SameBoyPlugPtr ignore);

	void updateLinkTargets();

	void setSampleRate(double sampleRate);

	SameBoyPlugPtr getPlug(size_t idx) { return _plugs[idx]; }

	SameBoyPlugPtr* plugs() { return _plugs; }

	const SameBoyPlugPtr* plugs() const { return _plugs; }
};
