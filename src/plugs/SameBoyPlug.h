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

enum class GameboyModel {
	Auto,
	DmgB,
	//SgbNtsc,
	//SgbPal,
	//Sgb2,
	CgbC,
	CgbE,
	Agb
};

const int FRAME_WIDTH = 160;
const int FRAME_HEIGHT = 144;

class SameBoyPlug;
using SameBoyPlugPtr = std::shared_ptr<SameBoyPlug>;

class SameBoyPlug {
private:
	void* _instance = nullptr;

	bool _midiSync = false;
	bool _gameLink = false;
	int _resetSamples = 0;

	GameboyModel _model = GameboyModel::Auto;

	double _sampleRate = 48000;

	Dimension2 _dimensions;
	VideoBuffer* _videoBuffer;
	AudioBuffer* _audioBuffer;

public:
	SameBoyPlug();
	~SameBoyPlug() { shutdown(); }

	void pressButtons(const StreamButtonPress* presses, size_t pressCount);

	void loadRom(const char* data, size_t size, bool fastBoot);

	bool watchRom() const { return false; }

	Dimension2 getDimensions() const { return _dimensions; }

	void setBuffers(VideoBuffer* video, AudioBuffer* audio) {
		_videoBuffer = video;
		_audioBuffer = audio;
	}

	void setModel(GameboyModel model) { _model = model; }

	GameboyModel model() const { return _model; }

	bool midiSync() { return _midiSync; }

	void setMidiSync(bool enabled) { _midiSync = enabled; }

	bool gameLink() const { return _gameLink; }

	void setGameLink(bool enabled) { _gameLink = enabled; }

	void init(const tstring& romPath, GameboyModel model, bool fastBoot);

	void reset(GameboyModel model, bool fast);

	bool active() const { return _instance != nullptr; }

	void setSampleRate(double sampleRate);
	 
	void sendKeyboardByte(int offset, char byte);

	void sendSerialByte(int offset, char byte, size_t bitCount = 8);

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

	void updateRom();

private:
	void updateButtons();

	void updateAV(int audioFrames);
};
