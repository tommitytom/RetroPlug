#pragma once

#include "platform/DynamicLibraryMemory.h"
#include "resource.h"

#define SAMEBOY_SYMBOLS(symb) getSymbols().symb

struct SameboyPlugSymbols {
	void*(*sameboy_init)(void* user_data, const char* romData, size_t romSize, int model, bool fast_boot);
	void(*sameboy_update_rom)(void* state, const char* rom_data, size_t rom_size);
	void(*sameboy_free)(void* state);
	void(*sameboy_reset)(void* state, int model, bool fast_boot);

	void(*sameboy_update)(void* state, size_t requiredAudioFrames);
	void(*sameboy_update_multiple)(void** states, size_t stateCount, size_t requiredAudioFrames);

	void(*sameboy_set_sample_rate)(void* state, double sample_rate);
	void(*sameboy_set_setting)(void* state, const char* name, int value);
	void(*sameboy_disable_rendering)(void* state, bool disabled);

	void(*sameboy_send_serial_byte)(void* state, int offset, char byte, size_t bitCount);
	void(*sameboy_set_midi_bytes)(void* state, int offset, const char* bytes, size_t count);
	void(*sameboy_set_button)(void* state, int offset, int buttonId, bool down);
	void(*sameboy_set_link_targets)(void* state, void** linkTargets, size_t count);

	size_t(*sameboy_battery_size)(void* state);
	void(*sameboy_load_battery)(void* state, const char* source, size_t size);
	size_t(*sameboy_save_battery)(void* state, char* target, size_t size);

	size_t(*sameboy_save_state_size)(void* state);
	void(*sameboy_load_state)(void* state, const char* source, size_t size);
	size_t(*sameboy_save_state)(void* state, char* target, size_t size);

	size_t(*sameboy_fetch_audio)(void* state, int16_t* audio);
	size_t(*sameboy_fetch_video)(void* state, uint32_t* video);
};

static SameboyPlugSymbols& getSymbols() {
	static DynamicLibraryMemory instance;
	static SameboyPlugSymbols _symbols = { nullptr };

	if (_symbols.sameboy_init) {
		return _symbols;
	}

	instance.load(IDR_RCDATA1);
	instance.get("sameboy_init", _symbols.sameboy_init);
	instance.get("sameboy_reset", _symbols.sameboy_reset);
	instance.get("sameboy_update", _symbols.sameboy_update);
	instance.get("sameboy_update_multiple", _symbols.sameboy_update_multiple);
	instance.get("sameboy_fetch_audio", _symbols.sameboy_fetch_audio);
	instance.get("sameboy_fetch_video", _symbols.sameboy_fetch_video);
	instance.get("sameboy_set_sample_rate", _symbols.sameboy_set_sample_rate);
	instance.get("sameboy_send_serial_byte", _symbols.sameboy_send_serial_byte);
	instance.get("sameboy_set_midi_bytes", _symbols.sameboy_set_midi_bytes);
	instance.get("sameboy_disable_rendering", _symbols.sameboy_disable_rendering);
	instance.get("sameboy_free", _symbols.sameboy_free);
	instance.get("sameboy_set_button", _symbols.sameboy_set_button);
	instance.get("sameboy_save_state_size", _symbols.sameboy_save_state_size);
	instance.get("sameboy_save_state", _symbols.sameboy_save_state);
	instance.get("sameboy_load_state", _symbols.sameboy_load_state);
	instance.get("sameboy_battery_size", _symbols.sameboy_battery_size);
	instance.get("sameboy_save_battery", _symbols.sameboy_save_battery);
	instance.get("sameboy_load_battery", _symbols.sameboy_load_battery);
	instance.get("sameboy_set_setting", _symbols.sameboy_set_setting);
	instance.get("sameboy_set_link_targets", _symbols.sameboy_set_link_targets);
	instance.get("sameboy_update_rom", _symbols.sameboy_update_rom);

	return _symbols;
}
