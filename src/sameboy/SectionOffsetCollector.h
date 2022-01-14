#ifndef SECTION_OFFSET_COLLECTOR_h
#define SECTION_OFFSET_COLLECTOR_h

#include <gb_struct_def.h>

typedef struct {
	int offset;
	int size;
} GB_section_offset_pair_t;

typedef struct {
	GB_section_offset_pair_t ram;
	GB_section_offset_pair_t coreState;
	GB_section_offset_pair_t dma;
	GB_section_offset_pair_t mbc;
	GB_section_offset_pair_t hram;
	GB_section_offset_pair_t timing;
	GB_section_offset_pair_t apu;
	GB_section_offset_pair_t rtc;
	GB_section_offset_pair_t video;
} GB_section_offsets_t;

void getSameboyStateOffsets(GB_gameboy_t* gb, GB_section_offsets_t* offsets);

#endif
