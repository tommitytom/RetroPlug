#ifdef __cplusplus
extern "C" {
#endif

#define GB_INTERNAL
#include <gb.h>

#include "SectionOffsetCollector.h"

void getSameboyStateOffsets(GB_gameboy_t* gb, GB_section_offsets_t* offsets) {
    uint32_t offset = GB_get_save_state_size_no_bess(gb);

    offset -= gb->vram_size;
    offsets->video.offset = offset;
    offsets->video.size = gb->vram_size;

    offset -= gb->ram_size;
    offsets->ram.offset = offset;
    offsets->ram.size = gb->ram_size;
    
    offset -= gb->mbc_ram_size;
    offsets->mbc.offset = offset;
    offsets->mbc.size = gb->mbc_ram_size;
}

#ifdef __cplusplus
}
#endif
