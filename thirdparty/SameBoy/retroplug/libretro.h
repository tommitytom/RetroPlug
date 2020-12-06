#ifndef LIBRETRO_H__
#define LIBRETRO_H__

#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <stdbool.h>

#define RETRO_API

#ifdef __cplusplus
extern "C" {
#endif


struct offset_byte_t {
    int offset;
    char byte;
    int bitCount;
};
typedef struct offset_byte_t offset_byte_t;

struct offset_button_t {
	int offset;
	int duration;
	int button;
	bool down;
};
typedef struct offset_button_t offset_button_t;

RETRO_API void* sameboy_init(void* user_data, const char* rom_data, size_t rom_size, int model, bool fast_boot);
RETRO_API void sameboy_update_rom(void* state, const char* rom_data, size_t rom_size);
RETRO_API void sameboy_free(void* state);
RETRO_API void sameboy_reset(void* state, int model, bool fast_boot);

RETRO_API void sameboy_set_link_targets(void* state, void** linkTargets, size_t count);
RETRO_API void sameboy_set_sample_rate(void* state, double sample_rate);
RETRO_API void sameboy_set_setting(void* state, const char* name, int value);
RETRO_API void sameboy_disable_rendering(void* state, bool disabled);

RETRO_API void sameboy_send_serial_byte(void* state, int offset, char byte, size_t bitCount);
RETRO_API void sameboy_set_midi_bytes(void* state, int offset, const char* byte, size_t count);
RETRO_API void sameboy_set_button(void* state, int duration, int buttonId, bool down);

RETRO_API size_t sameboy_battery_size(void* state);
RETRO_API size_t sameboy_save_battery(void* state, const char* target, size_t size);
RETRO_API void sameboy_load_battery(void* state, const char* source, size_t size);

RETRO_API size_t sameboy_save_state_size(void* state);
RETRO_API size_t sameboy_save_state(void* state, char* target, size_t size);
RETRO_API void sameboy_load_state(void* state, const char* source, size_t size);

RETRO_API void sameboy_update(void* state, size_t requiredAudioFrames);
RETRO_API void sameboy_update_multiple(void** states, size_t stateCount, size_t requiredAudioFrames);

RETRO_API size_t sameboy_fetch_audio(void* state, int16_t* audio);
RETRO_API size_t sameboy_fetch_video(void* state, uint32_t* video);

#ifdef __cplusplus
}
#endif

#endif
