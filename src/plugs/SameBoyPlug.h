#pragma once

#include "platform/DynamicLibrary.h"
#include "platform/DynamicLibraryMemory.h"
#include "libretroplug/MessageBus.h"
#include "roms/Lsdj.h"
#include <mutex>
#include <atomic>
#include <vector>

enum class GameboyModel {
	Auto,
	DmgB,
	//Sgb,
	//SgbNtsc,
	//SgbPal,
	//Sgb2,
	CgbC,
	CgbE,
	Agb
};

struct SameboyPlugSymbols {
	void*(*sameboy_init)(void* user_data, const char* path, int model);
	void(*sameboy_free)(void* state);
	void(*sameboy_reset)(void* state);

	void(*sameboy_update)(void* state, size_t requiredAudioFrames);
	void(*sameboy_update_multiple)(void** states, size_t stateCount, size_t requiredAudioFrames);

	void(*sameboy_set_sample_rate)(void* state, double sample_rate);
	void(*sameboy_set_setting)(void* state, const char* name, int value);

	void(*sameboy_set_midi_bytes)(void* state, int offset, const char* bytes, size_t count);
	void(*sameboy_set_button)(void* state, int buttonId, bool down);
	void(*sameboy_set_link_targets)(void* state, void** linkTargets, size_t count);

	size_t(*sameboy_battery_size)(void* state);
	void(*sameboy_load_battery)(void* state, const char* source, size_t size);
	size_t(*sameboy_save_battery)(void* state, const char* target, size_t size);

	size_t(*sameboy_save_state_size)(void* state);
	void(*sameboy_load_state)(void* state, const char* source, size_t size);
	void(*sameboy_save_state)(void* state, char* target, size_t size);

	size_t(*sameboy_fetch_audio)(void* state, int16_t* audio);
	size_t(*sameboy_fetch_video)(void* state, uint32_t* video);

	const char*(*sameboy_get_rom_name)(void* state);
};

class SameBoyPlug;
using SameBoyPlugPtr = std::shared_ptr<SameBoyPlug>;

class SameBoyPlug {
private:
	DynamicLibraryMemory _library;
	//DynamicLibrary _library;
	SameboyPlugSymbols _symbols = { nullptr };

	void* _instance = nullptr;
	void* _resampler = nullptr;

	std::string _romPath;
	std::string _savePath;
	std::string _romName;

	MessageBus _bus;

	std::mutex _lock;
	std::atomic<bool> _midiSync = false;
	std::atomic<bool> _gameLink = false;

	Lsdj _lsdj;

	double _sampleRate = 48000;

public:
	SameBoyPlug() {}
	~SameBoyPlug() { shutdown(); }

	Lsdj& lsdj() { return _lsdj; }

	bool midiSync() { return _midiSync.load(); }

	void setMidiSync(bool enabled) { _midiSync = enabled; }

	bool gameLink() const { return _gameLink.load(); }

	void setGameLink(bool enabled) { _gameLink = enabled; }

	void init(const std::string& romPath, GameboyModel model);

	bool active() const { return _instance != nullptr; }

	const std::string& romName() const { return _romName; }

	const std::string& romPath() const { return _romPath; }

	std::mutex& lock() { return _lock; }

	void setSampleRate(double sampleRate);

	void sendMidiByte(int offset, char byte) { sendMidiBytes(offset, &byte, 1); }

	void sendMidiBytes(int offset, const char* bytes, size_t count);

	size_t saveStateSize();

	bool saveBattery(const std::string& path);

	bool saveBattery(std::vector<char>& data);

	bool loadBattery(const std::string& path, bool reset);

	bool loadBattery(const std::vector<char>& path, bool reset);

	void saveState(char* target, size_t size);

	void loadState(const char* source, size_t size);

	void setSetting(const std::string& name, int value);

	void setLinkTargets(std::vector<SameBoyPlugPtr> linkTargets);

	void setButtonState(const ButtonEvent& ev) { _bus.buttons.writeValue(ev); }

	MessageBus* messageBus() { return &_bus; }

	void update(size_t audioFrames);

	void updateMultiple(SameBoyPlug** plugs, size_t plugCount, size_t audioFrames);

	void reset();

	void shutdown();

	void* instance() { return _instance; }

private:
	void updateButtons();

	void updateAV(int audioFrames);
};


