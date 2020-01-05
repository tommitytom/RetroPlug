#include "libretro.h"
#include "queue.h"

#include <Core/gb.h>
//#include <windows.h>

extern const unsigned char dmg_boot[], cgb_boot[], cgb_fast_boot[], agb_boot[], sgb_boot[], sgb2_boot[];
extern const unsigned dmg_boot_length, cgb_boot_length, cgb_fast_boot_length, agb_boot_length, sgb_boot_length, sgb2_boot_length;

static uint32_t rgbEncode(GB_gameboy_t* gb, uint8_t r, uint8_t g, uint8_t b) {
  return 255 << 24 | b << 16 | g << 8 | r;
}

int vasprintf(char **str, const char *fmt, va_list args)
{
	/* size_t size = _vscprintf(fmt, args) + 1;
	*str = (char*)malloc(size);
	int ret = vsprintf(*str, fmt, args);
	if (ret != size - 1) {
		free(*str);
		*str = NULL;
		return -1;
	}
	return ret;*/
    return 0;
}

#define PIXEL_WIDTH 160
#define PIXEL_HEIGHT 144
#define PIXEL_COUNT (PIXEL_WIDTH * PIXEL_HEIGHT)
#define FRAME_BUFFER_SIZE (PIXEL_COUNT * 4)

#define LINK_TICKS_MAX 3907

#define MAX_INSTANCES 4

#define MAX_SERIAL_ITEMS 128
#define MAX_BUTTON_ITEMS 64

typedef struct boot_rom_t {
    const unsigned char* data;
    size_t size;

    const unsigned char* fast_data;
    size_t fast_size;
} boot_rom_t;

boot_rom_t find_boot_rom(int model) {
    boot_rom_t boot_rom;
    memset(&boot_rom, 0, sizeof(boot_rom_t));

    switch (model) {
        case GB_MODEL_DMG_B:    boot_rom.data = dmg_boot; boot_rom.size = dmg_boot_length; break;
        case GB_MODEL_AGB:      boot_rom.data = agb_boot; boot_rom.size = agb_boot_length; break;
        case GB_MODEL_SGB_NTSC: boot_rom.data = sgb_boot; boot_rom.size = sgb_boot_length; break;
        case GB_MODEL_SGB_PAL:  boot_rom.data = sgb_boot; boot_rom.size = sgb_boot_length; break;
        case GB_MODEL_SGB2:     boot_rom.data = sgb2_boot; boot_rom.size = sgb2_boot_length; break;
        case GB_MODEL_CGB_E:
        case GB_MODEL_CGB_C:
            boot_rom.data = cgb_boot; boot_rom.size = cgb_boot_length;
            boot_rom.fast_data = cgb_fast_boot; boot_rom.fast_size = cgb_fast_boot_length;
            break;
    }

    if (boot_rom.fast_data == NULL) {
        boot_rom.fast_data = boot_rom.data;
        boot_rom.fast_size = boot_rom.size;
    }

    return boot_rom;
}

typedef struct sameboy_state_t {
    GB_gameboy_t gb;
    char frameBuffer[FRAME_BUFFER_SIZE];
    GB_sample_t audioBuffer[1024 * 8];
    size_t currentAudioFrames;
    Queue serialQueue;
    char serialQueueData[sizeof(offset_byte_t) * MAX_SERIAL_ITEMS];
    Queue buttonQueue;
    char buttonQueueData[sizeof(offset_button_t) * MAX_BUTTON_ITEMS];
    bool vblankOccurred;
    int linkTicksRemain;

    int processTicks;

    struct sameboy_state_t* linkTargets[MAX_INSTANCES];
    size_t linkTargetCount;
    bool bit_to_send;
} sameboy_state_t;

static void vblankHandler(GB_gameboy_t* gb) {
    sameboy_state_t* state = (sameboy_state_t*)GB_get_user_data(gb);
    state->vblankOccurred = true;
}

static void audioHandler(GB_gameboy_t* gb, GB_sample_t* sample) {
    sameboy_state_t* s = (sameboy_state_t*)GB_get_user_data(gb);
    s->audioBuffer[s->currentAudioFrames++] = *sample;
}

static void serial_start(GB_gameboy_t* gb, bool bit_received) {
    sameboy_state_t* s = (sameboy_state_t*)GB_get_user_data(gb);
    s->bit_to_send = bit_received;
}

static bool serial_end(GB_gameboy_t* gb) {
    sameboy_state_t* s = (sameboy_state_t*)GB_get_user_data(gb);

    bool ret = s->linkTargetCount > 0 ? GB_serial_get_data_bit(&s->linkTargets[0]->gb) : true;
    for (size_t i = 0; i < s->linkTargetCount; i++) {
        GB_serial_set_data_bit(&s->linkTargets[i]->gb, s->bit_to_send);
    }

    return ret;
}

void* sameboy_init(void* user_data, const char* rom_data, size_t rom_size, int model, bool fast_boot) {
    sameboy_state_t* state = malloc(sizeof(sameboy_state_t));

    state->vblankOccurred = false;
    state->currentAudioFrames = 0;
    state->linkTicksRemain = 0;
    state->bit_to_send = true;
    state->linkTargetCount = 0;
    state->processTicks = 0;

    GB_init(&state->gb, model);

    boot_rom_t boot_rom = find_boot_rom(model);
    if (!fast_boot) {
        GB_load_boot_rom_from_buffer(&state->gb, boot_rom.data, boot_rom.size);
    } else {
        GB_load_boot_rom_from_buffer(&state->gb, boot_rom.fast_data, boot_rom.fast_size);
    }

    GB_set_pixels_output(&state->gb, state->frameBuffer);
    GB_set_sample_rate(&state->gb, 48000);
    GB_set_user_data(&state->gb, state);

    GB_set_rgb_encode_callback(&state->gb, rgbEncode);
    GB_set_vblank_callback(&state->gb, vblankHandler);
    GB_apu_set_sample_callback(&state->gb, audioHandler);

    GB_set_color_correction_mode(&state->gb, GB_COLOR_CORRECTION_EMULATE_HARDWARE);
    GB_set_highpass_filter_mode(&state->gb, GB_HIGHPASS_ACCURATE);

    GB_set_serial_transfer_bit_start_callback(&state->gb, serial_start);
    GB_set_serial_transfer_bit_end_callback(&state->gb, serial_end);

    GB_set_rendering_disabled(&state->gb, true);

    GB_load_rom_from_buffer(&state->gb, rom_data, rom_size);

    queue_init(&state->serialQueue, state->serialQueueData, sizeof(offset_byte_t), MAX_SERIAL_ITEMS);
    queue_init(&state->buttonQueue, state->buttonQueueData, sizeof(offset_button_t), MAX_BUTTON_ITEMS);

    return state;
}

void sameboy_update_rom(void* state, const char* rom_data, size_t rom_size) {
    sameboy_state_t* s = (sameboy_state_t*)state;

    size_t size;
    uint16_t bank;
    void* rom = GB_get_direct_access(&s->gb, GB_DIRECT_ACCESS_ROM, &size, &bank);

    if (size <= rom_size) {
        memcpy(rom, rom_data, rom_size);
    }
}

void sameboy_disable_rendering(void* state, bool disabled) {
    sameboy_state_t* s = (sameboy_state_t*)state;
    GB_set_rendering_disabled(&s->gb, disabled);
}

void sameboy_reset(void* state, int model, bool fast_boot) {
    sameboy_state_t* s = (sameboy_state_t*)state;

    boot_rom_t boot_rom = find_boot_rom(model);
    if (!fast_boot) {
        GB_load_boot_rom_from_buffer(&s->gb, boot_rom.data, boot_rom.size);
    } else {
        GB_load_boot_rom_from_buffer(&s->gb, boot_rom.fast_data, boot_rom.fast_size);
    }

    GB_switch_model_and_reset(&s->gb, model);
}

void sameboy_set_link_targets(void* state, void** linkTargets, size_t count) {
    sameboy_state_t* s = (sameboy_state_t*)state;

    for (size_t i = 0; i < count; i++) {
        s->linkTargets[i] = (sameboy_state_t*)(linkTargets[i]);
    }

    s->linkTargetCount = count;
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

void sameboy_send_serial_byte(void* state, int offset, char byte, size_t bitCount) {
    sameboy_state_t* s = (sameboy_state_t*)state;

    if (!queue_full(&s->serialQueue)) {
        offset_byte_t ob;
        ob.offset = offset;
        ob.byte = byte;
        ob.bitCount = bitCount;
        queue_enqueue(&s->serialQueue, &ob);
    }
}

void sameboy_set_midi_bytes(void* state, int offset, const char* bytes, size_t count) {
    for (size_t i = 0; i < count; i++) {
        sameboy_send_serial_byte(state, offset, bytes[i], 8);
    }
}

void sameboy_set_button(void* state, int duration, int buttonId, bool down) {
    sameboy_state_t* s = (sameboy_state_t*)state;

    if (!queue_full(&s->buttonQueue)) {
        offset_button_t ob;
        ob.offset = 0;
        ob.duration = duration;
        ob.button = buttonId;
        ob.down = down;

        offset_button_t* b = (offset_button_t*)queue_back(&s->buttonQueue);
        if (b) {
            ob.offset = b->offset + b->duration;
        }

        queue_enqueue(&s->buttonQueue, &ob);
    }
}

size_t sameboy_save_state_size(void* state) {
    sameboy_state_t* s = (sameboy_state_t*)state;
    return GB_get_save_state_size(&s->gb);
}

size_t sameboy_battery_size(void* state) {
    sameboy_state_t* s = (sameboy_state_t*)state;
    return GB_battery_size(&s->gb);
}

size_t sameboy_save_battery(void* state, const char* target, size_t size) {
    sameboy_state_t* s = (sameboy_state_t*)state;
    return GB_save_battery_to_buffer(&s->gb, target, size) == 0;
}

void sameboy_load_battery(void* state, const char* source, size_t size) {
    sameboy_state_t* s = (sameboy_state_t*)state;
    GB_load_battery_from_buffer(&s->gb, source, size);
}

void sameboy_save_state(void* state, char* target, size_t size) {
    sameboy_state_t* s = (sameboy_state_t*)state;
    size_t state_size = GB_get_save_state_size(&s->gb);
    if (size >= state_size) {
        GB_save_state_to_buffer(&s->gb, target);
    }
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
        memcpy(audio, s->audioBuffer, s->currentAudioFrames * sizeof(GB_sample_t));
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

/*int update_first_instance(sameboy_state_t* s, int targetAudioFrames) {
    if (s->currentAudioFrames < targetAudioFrames) {
        s->processTicks += GB_run(&s->gb);
        return 0;
    }

    return 1;
}

int update_instance(sameboy_state_t* s, int targetAudioFrames, int targetTicks) {
    if (s->currentAudioFrames < targetAudioFrames) {
        while (s->currentAudioFrames < targetAudioFrames && s->processTicks < targetTicks) {
            s->processTicks += GB_run(&s->gb);
        }

        return 0;
    }

    return 1;
}*/

void sameboy_update_multiple(void** states, size_t stateCount, size_t requiredAudioFrames) {
    sameboy_state_t* st[MAX_INSTANCES];
    for (size_t i = 0; i < stateCount; i++) {
        st[i] = (sameboy_state_t*)states[i];
        st[i]->vblankOccurred = false;
    }

    size_t complete = 0;
    while (complete != stateCount) {
        complete = 0;
        for (size_t i = 0; i < stateCount; i++) {
            sameboy_state_t* s = st[i];
            if (s->currentAudioFrames < requiredAudioFrames) {
                GB_run(&s->gb);
            } else {
                complete++;
            }
        }
    }
}

void sameboy_update(void* state, size_t requiredAudioFrames) {
    sameboy_state_t* s = (sameboy_state_t*)state;

    s->vblankOccurred = false;

    int delta = 0;
    while (s->currentAudioFrames < requiredAudioFrames) {
        // Send bytes to the link port if required
        if (s->linkTicksRemain <= 0) {
            offset_byte_t* b = (offset_byte_t*)queue_front(&s->serialQueue);
            if (b && b->offset <= s->currentAudioFrames) {
                queue_dequeue(&s->serialQueue);

                for (int i = b->bitCount - 1; i >= 0; i--) {
                    bool bit = (bool)((b->byte & (1 << i)) >> i);
                    GB_serial_set_data_bit(&s->gb, bit);
                }
            }

            s->linkTicksRemain += LINK_TICKS_MAX;
        }

        // Send button presses if required
        offset_button_t* b = (offset_button_t*)queue_front(&s->buttonQueue);
        while (b && b->offset <= s->currentAudioFrames) {
            queue_dequeue(&s->buttonQueue);
            GB_set_key_state(&s->gb, b->button, b->down);
            b = (offset_button_t*)queue_front(&s->buttonQueue);
        }

        int ticks = GB_run(&s->gb);
        delta += ticks;
        s->linkTicksRemain -= ticks;
    }

    int buttonRemain = queue_length(&s->buttonQueue);
    for (int i = 0; i < buttonRemain; i++) {
        offset_button_t* b = (offset_button_t*)queue_get(&s->buttonQueue, i);
        b->offset -= s->currentAudioFrames;
    }

    // If there are any midi events that still haven't been processed, set their
    // offsets to 0 so they get processed immediately at the start of the next frame.
    int serialRemain = queue_length(&s->serialQueue);
    for (int i = 0; i < serialRemain; i++) {
        offset_byte_t* b = (offset_byte_t*)queue_get(&s->serialQueue, i);
        b->offset = 0;
    }
}

void sameboy_free(void* state) {
    sameboy_state_t* s = (sameboy_state_t*)state;
    GB_free(&s->gb);
    free(state);
}
