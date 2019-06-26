#ifndef LSDJ_ROM_H
#define LSDJ_ROM_H

#ifdef __cplusplus
extern "C" {
#endif	/* __cplusplus */

#include "liblsdj/error.h"
#include "liblsdj/vio.h"
#include "kit.h"

#define KIT_COUNT (64)
#define PALETTE_COUNT (6)
#define FONT_COUNT (3)

typedef struct lsdj_rom_t lsdj_rom_t;

lsdj_rom_t* lsdj_rom_read(lsdj_vio_t* vio, lsdj_error_t** error);
lsdj_rom_t* lsdj_rom_read_from_memory(const unsigned char* data, size_t size, lsdj_error_t** error);
lsdj_rom_t* lsdj_rom_read_from_file(const char* path, lsdj_error_t** error);

void lsdj_rom_write(const lsdj_rom_t* rom, lsdj_vio_t* vio, lsdj_error_t** error);

lsdj_kit_t* lsdj_rom_get_kit(lsdj_rom_t* rom, size_t idx);

void lsdj_rom_free(lsdj_rom_t* rom);

#ifdef __cplusplus
}
#endif

#endif
