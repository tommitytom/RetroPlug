#pragma once

#include <mutex>

// The one place BlastEm's C headers are seen from C++. Everything else in this package includes this header instead,
// so the extern "C" wrap is written once (the vendored headers carry no C++ guards of their own). Include this only
// from a target that links `blastem`: its PUBLIC definitions and include roots are what make these headers parse
// (without IS_LIB, render.h pulls in <SDL.h>; without NEW_CORE, genesis.h takes its JIT branch).

namespace rp::blastem {

// BlastEm's frontend callbacks take no instance argument, so the callbacks that have none of their own
// (render_get_framebuffer, render_framebuffer_updated, the audio source entry points) read this. One instance runs one
// call at a time; BlastemShim.cpp defines it, and every system method sets it on entry. Declared here, where every
// translation unit that calls into the core can see it: a definition with no declaration is not a seam.
extern thread_local void* currentInstance;

// Sets this thread's instance pointer, from a system on every call into the core. It is this thread's value from then
// until something else on this thread sets it, and destroyCore is the only place that clears it: a block or a state
// load leaves it naming the instance that ran, which is harmless because every entry sets it before the core can call
// back. It used to set BlastEm's global `current_system` beside it, because genesis.c's 68k-to-VDP DMA read path
// (read_dma_value) and vdp_dma_started() cast that global to their context; the mirror now hands those calls the VDP's
// own system pointer, and nothing in this build's file set reads current_system anymore.
void useInstance(void* instance);

// One process-wide lock over BlastEm's allocation path (alloc_config_genesis and free_context). That path still
// touches file-scope state this package has not audited end to end (the m68k sync/int-ack handoff, the YM and VDP
// table inits), so no two instances build or free a context at the same time and those statics are never shared
// state in practice. Control thread only: a block never allocates a context, so the audio path never takes it.
std::mutex& coreLifecycleMutex();

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
