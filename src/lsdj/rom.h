#ifndef LSDJ_ROM_H
#define LSDJ_ROM_H

#ifdef __cplusplus
extern "C" {
#endif	/* __cplusplus */

#include "liblsdj/error.h"
#include "liblsdj/vio.h"
#include "kit.h"

#define BANK_COUNT (64)
#define PALETTE_COUNT (6)
#define FONT_COUNT (3)

// Representation of an entire LSDJ rom file
typedef struct 
{
	lsdj_kit_t* kits[BANK_COUNT];

	size_t kit_count;

	//lsdj_palette_t* palettes[PALETTE_COUNT];

	//lsdj_font_t* fonts[FONT_COUNT];
} lsdj_rom_t;

lsdj_rom_t* lsdj_rom_read(lsdj_vio_t* vio, lsdj_error_t** error);
lsdj_rom_t* lsdj_rom_read_from_memory(const unsigned char* data, size_t size, lsdj_error_t** error);
lsdj_rom_t* lsdj_rom_read_from_file(const char* path, lsdj_error_t** error);

void lsdj_rom_patch(const lsdj_rom_t* rom, lsdj_vio_t* vio, lsdj_error_t** error);

void lsdj_rom_free(lsdj_rom_t* rom);

#ifdef __cplusplus
}
#endif

#endif
