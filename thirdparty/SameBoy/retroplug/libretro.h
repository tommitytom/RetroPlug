#ifndef LIBRETRO_H__
#define LIBRETRO_H__

#include <stdint.h>
#include <stddef.h>
#include <limits.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __cplusplus
#if defined(_MSC_VER) && !defined(SN_TARGET_PS3)
/* Hack applied for MSVC when compiling in C89 mode
 * as it isn't C99-compliant. */
#define bool unsigned char
#define true 1
#define false 0
#else
#include <stdbool.h>
#endif
#endif

#ifndef RETRO_CALLCONV
#  if defined(__GNUC__) && defined(__i386__) && !defined(__x86_64__)
#    define RETRO_CALLCONV __attribute__((cdecl))
#  elif defined(_MSC_VER) && defined(_M_X86) && !defined(_M_X64)
#    define RETRO_CALLCONV __cdecl
#  else
#    define RETRO_CALLCONV /* all other platforms only have one calling convention each */
#  endif
#endif

#ifndef RETRO_API
#  if defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
#    ifdef RETRO_IMPORT_SYMBOLS
#      ifdef __GNUC__
#        define RETRO_API RETRO_CALLCONV __attribute__((__dllimport__))
#      else
#        define RETRO_API RETRO_CALLCONV __declspec(dllimport)
#      endif
#    else
#      ifdef __GNUC__
#        define RETRO_API RETRO_CALLCONV __attribute__((__dllexport__))
#      else
#        define RETRO_API RETRO_CALLCONV __declspec(dllexport)
#      endif
#    endif
#  else
#      if defined(__GNUC__) && __GNUC__ >= 4 && !defined(__CELLOS_LV2__)
#        define RETRO_API RETRO_CALLCONV __attribute__((__visibility__("default")))
#      else
#        define RETRO_API RETRO_CALLCONV
#      endif
#  endif
#endif

RETRO_API void* sameboy_init(void* user_data, const char* path);

RETRO_API void sameboy_reset(void* state);

RETRO_API void sameboy_update(void* state, size_t requiredAudioFrames);

RETRO_API void sameboy_update_multiple(void** states, size_t stateCount, size_t requiredAudioFrames);

RETRO_API void sameboy_set_sample_rate(void* state, double sample_rate);

RETRO_API void sameboy_set_midi_bytes(void* state, int offset, const char* byte, size_t count);

RETRO_API void sameboy_set_button(void* state, int buttonId, bool down);

RETRO_API void sameboy_save_battery(void* state, const char* path);

RETRO_API void sameboy_load_battery(void* state, const char* path);

RETRO_API void sameboy_set_link_target(void* state, void* linkTarget);

RETRO_API size_t sameboy_save_state_size(void* state);

RETRO_API void sameboy_save_state(void* state, char* target, size_t size);

RETRO_API void sameboy_load_state(void* state, const char* source, size_t size);

RETRO_API size_t sameboy_fetch_audio(void* state, int16_t* audio);

RETRO_API size_t sameboy_fetch_video(void* state, uint32_t* video);

RETRO_API void sameboy_set_setting(void* state, const char* name, int value);

RETRO_API void sameboy_free(void* state);

RETRO_API const char* sameboy_get_rom_name(void* state);

#ifdef __cplusplus
}
#endif

#endif
