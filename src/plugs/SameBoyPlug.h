#pragma once

#include <vector>
#include <queue>
#include <gb_struct_def.h>

#include "retroplug/Messages.h"

const size_t PIXEL_WIDTH = 160;
const size_t PIXEL_HEIGHT = 144;
const size_t PIXEL_COUNT = (PIXEL_WIDTH * PIXEL_HEIGHT);
const size_t FRAME_BUFFER_SIZE = (PIXEL_COUNT * 4);
const size_t AUDIO_SCRATCH_SIZE = 1024 * 8;

class SameBoyPlug;
using SameBoyPlugPtr = std::shared_ptr<SameBoyPlug>;

struct SameBoyPlugDesc {
	std::string romName;
};

struct OffsetButton {
	int offset;
	int duration;
	int button;
	bool down;
};

struct OffsetByte {
	int offset;
	char byte;
	int bitCount;
};

struct GameboySample {
	int16_t left;
	int16_t right;
};

struct SameBoyPlugState {
	GB_gameboy_t* gb = nullptr;
	char frameBuffer[FRAME_BUFFER_SIZE];
	GameboySample audioBuffer[AUDIO_SCRATCH_SIZE];
	size_t currentAudioFrames = 0;
	std::queue<OffsetButton> buttonQueue;
	std::queue<OffsetByte> serialQueue;
	bool vblankOccurred = false;
	int linkTicksRemain = 0;

	GameboyModel model = GameboyModel::Auto;
	bool fastBoot = false;

	int processTicks = 0;

	std::vector<SameBoyPlugState*> linkTargets;
	
	bool bitToSend;
};

class SameBoyPlug {
private:
	SameBoyPlugState _state;

	SameBoyPlugDesc _desc;
	SameBoySettings _settings;

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

	void loadRom(const char* data, size_t size, const SameBoySettings& settings, bool fastBoot);

	bool watchRom() const { return false; }

	Dimension2 getDimensions() const { return _dimensions; }

	SameBoyPlugState* getState() { return &_state; }

	void setBuffers(VideoBuffer* video, AudioBuffer* audio) {
		_videoBuffer = video;
		_audioBuffer = audio;
	}

	const SameBoySettings& getSettings() const { return _settings; }

	void setSettings(const SameBoySettings& settings) { _settings = settings; }

	void reset(GameboyModel model, bool fast);

	bool active() const { return _state.gb != nullptr; }

	void setSampleRate(double sampleRate);

	void sendSerialByte(int offset, int byte);

	size_t saveStateSize();

	size_t batterySize();

	size_t saveBattery(char* data, size_t size);

	bool loadBattery(const char* data, size_t size, bool reset);

	bool clearBattery(bool reset);

	size_t saveState(char* target, size_t size);

	void loadState(const char* source, size_t size);

	void setSetting(const std::string& name, int value);

	void setLinkTargets(std::vector<SameBoyPlugPtr> linkTargets);

	void update(size_t audioFrames);

	void updateMultiple(SameBoyPlug** plugs, size_t plugCount, size_t audioFrames);

	void shutdown();

	void disableRendering(bool disable);

	void setRomData(DataBuffer<char>* data);

private:
	void updateAV(int audioFrames);

	void init(GameboyModel model);
};
