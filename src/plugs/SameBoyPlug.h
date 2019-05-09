#pragma once

#include "platform/DynamicLibrary.h"
#include "platform/DynamicLibraryMemory.h"
#include "libretroplug/MessageBus.h"
#include <mutex>

struct SameboyPlugSymbols {
	void*(*sameboy_init)(void* user_data, const char* path);
	void(*sameboy_reset)(void* state);
	void(*sameboy_update)(void* state, size_t requiredAudioFrames);
	size_t(*sameboy_fetch_audio)(void* state, int16_t* audio);
	size_t(*sameboy_fetch_video)(void* state, uint32_t* video);
	void(*sameboy_set_sample_rate)(void* state, double sample_rate);
	void(*sameboy_free)(void* state);
	void(*sameboy_set_midi_bytes)(void* state, int offset, const char* bytes, size_t count);
	void(*sameboy_set_button)(void* state, int buttonId, bool down);
	size_t(*sameboy_save_state_size)(void* state);
	void(*sameboy_save_state)(void* state, char* target, size_t size);
	void(*sameboy_load_state)(void* state, const char* source, size_t size);
	void(*sameboy_load_battery)(void* state, const char* path);
	void(*sameboy_save_battery)(void* state, const char* path);
	void(*sameboy_set_setting)(void* state, const char* name, int value);
	const char*(*sameboy_get_rom_name)(void* state);
};

class SameBoyPlug {
private:
	DynamicLibraryMemory _library;
	//DynamicLibrary _library;
	SameboyPlugSymbols _symbols = { nullptr };

	void* _instance = nullptr;
	void* _resampler = nullptr;
	std::string _romName;

	MessageBus _bus;

	std::mutex _lock;

public:
	SameBoyPlug() {}
	~SameBoyPlug() { shutdown(); }

	void init(const std::string& gamePath);

	bool active() const { return _instance != nullptr; }

	const std::string& romName() const { return _romName; }

	std::mutex& lock() { return _lock; }

	void setSampleRate(double sampleRate);

	void sendMidiByte(int offset, char byte) { sendMidiBytes(offset, &byte, 1); }

	void sendMidiBytes(int offset, const char* bytes, size_t count);

	size_t saveStateSize();

	void saveBattery(const std::string& path);

	void loadBattery(const std::string& path);

	void saveState(char* target, size_t size);

	void loadState(const char* source, size_t size);

	void setSetting(const std::string& name, int value);

	void setOversample(int value);

	MessageBus* messageBus() { return &_bus; }

	void update(size_t audioFrames);

	void shutdown();
};

using SameBoyPlugPtr = std::shared_ptr<SameBoyPlug>;
