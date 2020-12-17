#pragma once

#include "plugs/SameBoyPlug.h"
#include "messaging.h"
#include "Constants.h"
#include "Types.h"
#include "micromsg/allocator/allocator.h"

struct AudioSettings {
	size_t channelCount;
	size_t frameCount;
	double sampleRate;
};

class ProcessingContext {
private:
	std::vector<SameBoyPlugPtr> _systems;
	Node* _node = nullptr;

	Project::Settings _settings;
	AudioSettings _audioSettings;

	AudioBuffer _audioBuffers[MAX_SYSTEMS];
	GameboyButtonStream _buttonPresses[MAX_SYSTEMS];

	micromsg::Allocator* _alloc = nullptr;

public:
	ProcessingContext();

	~ProcessingContext();

	void setNode(Node* node) {
		_node = node;
		_alloc = node->getAllocator();
	}

	SameBoyPlugPtr& getSystem(SystemIndex idx) { return _systems[idx]; }

	const Project::Settings& getSettings() const { return _settings; }

	void setSettings(const Project::Settings& settings) { _settings = settings; }

	void setSystemSettings(SystemIndex idx, SameBoySettings settings);

	void setRenderingEnabled(bool enabled);

	GameboyButtonStream* getButtonPresses(SystemIndex idx) {
		return &_buttonPresses[idx];
	}

	void fetchState(const FetchStateRequest& req, FetchStateResponse& state);

	void setAudioSettings(const AudioSettings& settings);

	SameBoyPlugPtr swapSystem(SystemIndex idx, SameBoyPlugPtr instance);

	SameBoyPlugPtr duplicateSystem(SystemIndex sourceIdx, SystemIndex targetIdx, SameBoyPlugPtr system);

	void resetSystem(SystemIndex idx, GameboyModel model);

	SameBoyPlugPtr removeSystem(SystemIndex idx);

	void process(float** outputs, size_t frameCount);

private:
	void getLinkTargets(std::vector<SameBoyPlugPtr>& targets, SameBoyPlugPtr ignore);

	void updateLinkTargets();
};
