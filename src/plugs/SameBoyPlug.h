#pragma once

#include "util/MessageBus.h"
#include "roms/Lsdj.h"
#include "util/xstring.h"
#include "controller/messaging.h"
#include "model/ButtonStream.h"
#include <mutex>
#include <atomic>
#include <vector>

namespace sol {
	class state;
};

const int FRAME_WIDTH = 160;
const int FRAME_HEIGHT = 144;

class SameBoyPlug;
using SameBoyPlugPtr = std::shared_ptr<SameBoyPlug>;

struct SameBoyPlugDesc {
	std::string romName;
};

class SameBoyPlug {
private:
	void* _instance = nullptr;

	SameBoyPlugDesc _desc;
	SameBoySettings _settings;

	bool _midiSync = false;
	int _resetSamples = 0;

	double _sampleRate = 48000;

	Dimension2 _dimensions;
	VideoBuffer* _videoBuffer;
	AudioBuffer* _audioBuffer;

	int16_t* _audioScratch = nullptr;
	size_t _audioScratchSize = 0;

public:
	SameBoyPlug();
	~SameBoyPlug() { shutdown(); }

	const SameBoyPlugDesc& getDesc() const { return _desc; }

	void setDesc(const SameBoyPlugDesc& desc) { _desc = desc; }

	void pressButtons(const StreamButtonPress* presses, size_t pressCount);

	void loadRom(const char* data, size_t size, GameboyModel model, bool fastBoot);

	bool watchRom() const { return false; }

	Dimension2 getDimensions() const { return _dimensions; }

	void setBuffers(VideoBuffer* video, AudioBuffer* audio) {
		_videoBuffer = video;
		_audioBuffer = audio;
	}

	bool midiSync() { return _midiSync; }

	void setMidiSync(bool enabled) { _midiSync = enabled; }

	const SameBoySettings& getSettings() const { return _settings; }

	void setSettings(const SameBoySettings& settings) { _settings = settings; }

	void reset(GameboyModel model, bool fast);

	bool active() const { return _instance != nullptr; }

	void setSampleRate(double sampleRate);
	 
	void sendKeyboardByte(int offset, char byte);

	void sendSerialByte(int offset, int byte);

	void sendMidiBytes(int offset, const char* bytes, size_t count);

	size_t saveStateSize();

	size_t batterySize();

	bool saveBattery(char* data, size_t size);

	bool loadBattery(const char* data, size_t size, bool reset);

	bool clearBattery(bool reset);

	void saveState(char* target, size_t size);

	void loadState(const char* source, size_t size);

	void setSetting(const std::string& name, int value);

	void setLinkTargets(std::vector<SameBoyPlugPtr> linkTargets);

	void update(size_t audioFrames);

	void updateMultiple(SameBoyPlug** plugs, size_t plugCount, size_t audioFrames);

	void shutdown();

	void* instance() { return _instance; }

	void disableRendering(bool disable);

	void setRomData(DataBuffer<char>* data);

private:
	void updateAV(int audioFrames);
};
