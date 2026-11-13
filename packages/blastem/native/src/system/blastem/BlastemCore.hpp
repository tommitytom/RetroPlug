#pragma once

// The one place BlastEm's C headers are seen from C++. Everything else in this package includes this header instead,
// so the extern "C" wrap is written once (the vendored headers carry no C++ guards of their own).

namespace rp::blastem {

// BlastEm's frontend callbacks take no instance argument, so the callbacks that have none of their own
// (render_get_framebuffer, render_framebuffer_updated, io_custom_input, the audio source entry points) read this. One
// instance runs one call at a time; BlastemShim.cpp defines it, and every system method sets it on entry. Declared here,
// where every translation unit that calls into the core can see it: a definition with no declaration is not a seam.
extern thread_local void* currentInstance;

}  // namespace rp::blastem

extern "C" {
#include "genesis.h"      // genesis_context, alloc_config_genesis, MCLKS_PER_*
#include "io.h"           // io_port, io_data_read/write, io_control_write
#include "m68k.h"          // the generated core: m68k_reset, m68k_execute, m68k_context
#include "render.h"        // pixel_t, FRAMEBUFFER_*, render_get_framebuffer, render_framebuffer_updated
#include "render_audio.h"  // audio_source, render_audio_source, render_put_stereo_sample
#include "serialize.h"     // serialize_buffer, deserialize_buffer, init_serialize/init_deserialize
#include "system.h"        // system_header
#include "util.h"          // fatal_error, warning
#include "vdp.h"           // vdp_context, read_dma_value, vdp_cycles_to_frame_end
}
