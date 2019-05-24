#include "libretro.h"
#include "queue.h"

#include <Core/gb.h>
#include <windows.h>

extern const unsigned char dmg_boot[], cgb_boot[], agb_boot[], sgb_boot[], sgb2_boot[];
extern const unsigned dmg_boot_length, cgb_boot_length, agb_boot_length, sgb_boot_length, sgb2_boot_length;

static uint32_t rgbEncode(GB_gameboy_t* gb, uint8_t r, uint8_t g, uint8_t b) {
  return r << 16 | g << 8 | b;
}

#define PIXEL_WIDTH 160
#define PIXEL_HEIGHT 144
#define PIXEL_COUNT (PIXEL_WIDTH * PIXEL_HEIGHT)
#define FRAME_BUFFER_SIZE (PIXEL_COUNT * 4)

#define LINK_TICKS_MAX 3907

typedef struct sameboy_state_t {
    GB_gameboy_t gb;
    char frameBuffer[FRAME_BUFFER_SIZE];
    GB_sample_t audioBuffer[1024 * 8];
    size_t currentAudioFrames;
    Queue midiQueue;
    bool vblankOccurred;
    int linkTicksRemain;
} sameboy_state_t;

static void vblankHandler(GB_gameboy_t* gb) {
    sameboy_state_t* state = (sameboy_state_t*)GB_get_user_data(gb);
    state->vblankOccurred = true;
}

void* sameboy_init(void* user_data, const char* path) {
    sameboy_state_t* state = malloc(sizeof(sameboy_state_t));

    state->vblankOccurred = false;
    state->currentAudioFrames = 0;
    state->linkTicksRemain = 0;

    GB_init(&state->gb, GB_MODEL_CGB_E);
	GB_load_boot_rom_from_buffer(&state->gb, cgb_boot, cgb_boot_length);

    GB_set_pixels_output(&state->gb, state->frameBuffer);
    GB_set_sample_rate(&state->gb, 48000);
    GB_set_user_data(&state->gb, state);

    GB_set_rgb_encode_callback(&state->gb, rgbEncode);
    GB_set_vblank_callback(&state->gb, vblankHandler);

    GB_set_color_correction_mode(&state->gb, GB_COLOR_CORRECTION_EMULATE_HARDWARE);
    GB_set_highpass_filter_mode(&state->gb, GB_HIGHPASS_ACCURATE);

    if (GB_load_rom(&state->gb, path)) {
        free(state);
        return NULL;
    }

    queue_init(&state->midiQueue);

    return state;
}

void sameboy_reset(void* state) {
    sameboy_state_t* s = (sameboy_state_t*)state;
    GB_reset(&s->gb);
}

void sameboy_set_setting(void* state, const char* name, int value) {
    sameboy_state_t* s = (sameboy_state_t*)state;
    if (strcmp(name, "Color Correction") == 0) {
        GB_set_color_correction_mode(&s->gb, value);
    } else if (strcmp(name, "High-pass Filter") == 0) {
        GB_set_highpass_filter_mode(&s->gb, value);
    }
}

void sameboy_set_sample_rate(void* state, double sample_rate) {
    sameboy_state_t* s = (sameboy_state_t*)state;
    GB_set_sample_rate(&s->gb, (uint32_t)sample_rate);
}

void sameboy_set_midi_bytes(void* state, int offset, const char* bytes, size_t count) {
    sameboy_state_t* s = (sameboy_state_t*)state;

    for (size_t i = 0; i < count; i++) {
        if (length(&s->midiQueue) < MAX_QUEUE_SIZE) {
            offset_byte_t ob;
            ob.offset = offset;
            ob.byte = bytes[i];
            enqueue(&s->midiQueue, ob);
        }
    }
}

void sameboy_set_button(void* state, int buttonId, bool down) {
    sameboy_state_t* s = (sameboy_state_t*)state;
    GB_set_key_state_for_player(&s->gb, buttonId,  0, down);
}

size_t sameboy_save_state_size(void* state) {
    sameboy_state_t* s = (sameboy_state_t*)state;
    return GB_get_save_state_size(&s->gb);
}

void sameboy_save_battery(void* state, const char* path) {
    sameboy_state_t* s = (sameboy_state_t*)state;
    GB_save_battery(&s->gb, path);
}

void sameboy_load_battery(void* state, const char* path) {
    sameboy_state_t* s = (sameboy_state_t*)state;
    GB_load_battery(&s->gb, path);
}

void sameboy_save_state(void* state, char* target, size_t size) {
    sameboy_state_t* s = (sameboy_state_t*)state;
    size_t state_size = GB_get_save_state_size(&s->gb);
    GB_save_state_to_buffer(&s->gb, target);
}

void sameboy_load_state(void* state, const char* source, size_t size) {
    sameboy_state_t* s = (sameboy_state_t*)state;
    size_t state_size = GB_get_save_state_size(&s->gb);
    GB_load_state_from_buffer(&s->gb, source, size);
}

size_t sameboy_fetch_audio(void* state, int16_t* audio) {
    sameboy_state_t* s = (sameboy_state_t*)state;
    size_t size = s->currentAudioFrames;
    if (size > 0) {
        GB_apu_copy_buffer(&s->gb, audio, s->currentAudioFrames);
        s->currentAudioFrames = 0;
    }

    return size;
}

size_t sameboy_fetch_video(void* state, uint32_t* video) {
    sameboy_state_t* s = (sameboy_state_t*)state;
    if (s->vblankOccurred) {
        memcpy(video, s->frameBuffer, FRAME_BUFFER_SIZE);
        return FRAME_BUFFER_SIZE;
    }

    return 0;
}

bool flip = false;

void sameboy_update(void* state, size_t requiredAudioFrames) {
    sameboy_state_t* s = (sameboy_state_t*)state;

    s->vblankOccurred = false;

    int delta = 0;
    while (s->currentAudioFrames < requiredAudioFrames) {
        if (s->linkTicksRemain <= 0) {
            if (length(&s->midiQueue) && peek(&s->midiQueue).offset <= s->currentAudioFrames) {
                offset_byte_t b = dequeue(&s->midiQueue);

                if (flip) {
                    GB_serial_set_data_bit(&s->gb, 1);
                    GB_serial_set_data_bit(&s->gb, 1);
                } else {
                    GB_serial_set_data_bit(&s->gb, 1);
                    GB_serial_set_data_bit(&s->gb, 0);
                }

                flip = !flip;

                /*for (int i = 7; i >= 0; i--) {
                    bool bit = (bool)((b.byte & (1 << i)) >> i);
                    GB_serial_set_data_bit(&s->gb, bit);
                }*/
            }

            s->linkTicksRemain += LINK_TICKS_MAX;
        }

        int ticks = GB_run(&s->gb);
        delta += ticks;
        s->linkTicksRemain -= ticks;

        s->currentAudioFrames = GB_apu_get_current_buffer_length(&s->gb);
    }

    // If there are any midi events that still haven't been processed, set their
    // offsets to 0 so they get processed immediately at the start of the next frame.
    if (length(&s->midiQueue)) {
        for (int i = 0; i < MAX_QUEUE_SIZE; i++) {
            s->midiQueue.data[i].offset = 0;
        }
    }
}

const char* sameboy_get_rom_name(void* state) {
    sameboy_state_t* s = (sameboy_state_t*)state;
    return (const char*)(s->gb.rom + 0x134);
}

void sameboy_free(void* state) {
    sameboy_state_t* s = (sameboy_state_t*)state;
    GB_free(&s->gb);
    free(state);
}
