#include <gb.h>

#include "SectionOffsetCollector.h"

void getSameboyStateOffsets(GB_gameboy_t* gb, GB_section_offsets_t* offsets) {
    size_t offset = GB_SECTION_SIZE(header) +
        GB_SECTION_SIZE(core_state) + sizeof(uint32_t) +
        GB_SECTION_SIZE(dma) + sizeof(uint32_t) +
        GB_SECTION_SIZE(mbc) + sizeof(uint32_t) +
        GB_SECTION_SIZE(hram) + sizeof(uint32_t) +
        GB_SECTION_SIZE(timing) + sizeof(uint32_t) +
        GB_SECTION_SIZE(apu) + sizeof(uint32_t) +
        GB_SECTION_SIZE(rtc) + sizeof(uint32_t) +
        GB_SECTION_SIZE(video) + sizeof(uint32_t);

    if (GB_is_hle_sgb(gb)) {
        offset += sizeof(*gb->sgb) + sizeof(uint32_t);
    }

    offsets->mbc.offset = offset;
    offsets->mbc.size = gb->mbc_ram_size;
    offset += gb->mbc_ram_size;

    offsets->ram.offset = offset;
    offsets->ram.size = gb->ram_size;
    offset += gb->ram_size;

    offsets->video.offset = offset;
    offsets->video.size = gb->vram_size;
    //offset += gb->vram_size;
}
