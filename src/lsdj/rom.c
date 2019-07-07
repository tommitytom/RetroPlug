#include "rom.h"
#include "kit.h"
#include "sample.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// one colorset contains 4 colors
#define COLOR_SET_SIZE (4 * 2)  
// one palette contains 5 color sets
#define COLOR_SET_COUNT (5)
#define PALETTE_SIZE (COLOR_SET_COUNT * COLOR_SET_SIZE)
#define PALETTE_COUNT (6)
#define PALETTE_NAME_SIZE (4)

#define FONT_COUNT (3)
#define FONT_HEADER_SIZE (130)
#define FONT_SIZE (0xE96)
#define FONT_TILE_COUNT (71)
#define FONT_TILE_SIZE (16)
#define TILE_WIDTH (8)
#define TILE_HEIGHT (8)
#define FONT_NAME_SIZE (4)
#define ALTERNATE_FONT_OFFSET (0x4D2)

lsdj_rom_t* lsdj_rom_read(lsdj_vio_t* vio, lsdj_error_t** error)
{
	lsdj_rom_t* rom = malloc(sizeof(lsdj_rom_t));
	memset(rom, 0, sizeof(lsdj_rom_t));


	for (size_t i = 0; i < BANK_COUNT; ++i) {
		size_t offset = i * BANK_SIZE;
		vio->seek(offset, SEEK_SET, vio->user_data);
		lsdj_kit_t* kit = lsdj_kit_read(vio, error);
		if (kit) {
			rom->kits[rom->kit_count++] = kit;
		}
	}

	return rom;
}

void lsdj_rom_patch(const lsdj_rom_t* rom, lsdj_vio_t* vio, lsdj_error_t** error) 
{
	for (size_t i = 0; i < BANK_COUNT; ++i) {
		size_t offset = i * BANK_SIZE;
		vio->seek(offset, SEEK_SET, vio->user_data);
		if (rom->kits[i]) {
			lsdj_kit_write(rom->kits[i], vio, error);
		} else {
			char empty[BANK_SIZE] = { 0 };
			vio->write(empty, BANK_SIZE, vio->user_data);
		}
	}
}

lsdj_rom_t* lsdj_rom_read_from_memory(const unsigned char* data, size_t size, lsdj_error_t** error)
{
	if (data == NULL)
	{
		lsdj_error_new(error, "data is NULL");
		return NULL;
	}

	lsdj_memory_data_t mem;
	mem.begin = (unsigned char*)data;
	mem.cur = mem.begin;
	mem.size = size;

	lsdj_vio_t vio;
	vio.read = lsdj_mread;
	vio.tell = lsdj_mtell;
	vio.seek = lsdj_mseek;
	vio.user_data = &mem;

	return lsdj_rom_read(&vio, error);
}

lsdj_rom_t* lsdj_rom_read_from_file(const char* path, lsdj_error_t** error)
{
	if (path == NULL)
	{
		lsdj_error_new(error, "path is NULL");
		return NULL;
	}

	FILE* file = fopen(path, "rb");
	if (file == NULL)
	{
		char message[512];
		snprintf(message, 512, "could not open %s for reading", path);
		lsdj_error_new(error, message);
		return NULL;
	}

	lsdj_vio_t vio;
	vio.read = lsdj_fread;
	vio.tell = lsdj_ftell;
	vio.seek = lsdj_fseek;
	vio.user_data = file;

	lsdj_rom_t* rom = lsdj_rom_read(&vio, error);

	fclose(file);
	return rom;
}

lsdj_kit_t* lsdj_rom_get_kit(lsdj_rom_t* rom, size_t idx) {
	return rom->kits[idx];
}

void lsdj_rom_free(lsdj_rom_t* rom) {
	if (rom) {
		for (size_t i = 0; i < BANK_COUNT; ++i) {
			if (rom->kits[i]) {
				lsdj_kit_free(rom->kits[i]);
			}
		}

		free(rom);
	}
}
