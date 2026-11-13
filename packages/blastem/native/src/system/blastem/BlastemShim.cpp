// BlastEm's frontend seam, for a host that is RetroPlug rather than BlastEm's own SDL/GTK shell. Two halves:
//
//   1. What every build needs and libblastem.c would otherwise provide: the config tree, the globals genesis.c and
//      io.c read (current_system, headless, z80_enabled, ...), read_bundled_file() for the ROM database, and the render
//      callbacks. They stay here because they are extern "C" and so may live in exactly one file; the video ones are
//      real and delegate to the sink in BlastemVideo.cpp, and the audio ones are defaults until the audio sink lands.
//   2. Stubs for the systems this build does not compile. genesis.c, romdb.c and io.c still reference them, but only
//      behind a field a Mega Drive never sets (gen->mars for the 32X, gen->expansion for the Sega CD, a mapper type
//      only a Pico, Coleco, LaserActive or SMS cart selects), so no emulated Mega Drive instruction reaches one.
//      Each group says which field keeps it unreachable.
#include "system/blastem/BlastemCore.hpp"

#include <cstdlib>
#include <cstring>

#include "system/blastem/BlastemMdSystem.hpp"  // the instance whose video() the render callbacks answer for

namespace rp::blastem {

// The instance the frontend callbacks belong to (declared in BlastemCore.hpp).
thread_local void* currentInstance = nullptr;

}  // namespace rp::blastem

extern "C" {

// The headers of the systems the build leaves out, plus the two whose declarations this file's definitions have to be
// checked against. They belong here and not in BlastemCore.hpp because nothing else in the package names their types:
// a core translation unit that could reach one of them would have its system's source compiled.
#include "blastem.h"  // the globals (config, current_system, headless, ...) and current_media
#include "coleco.h"
#include "gen_player.h"
#include "korean_sms_multi.h"
#include "laseractive.h"
#include "mediaplayer.h"
#include "net.h"  // iface_info, and the socket_* seam event_log.c and megawifi.c call
#include "paths.h"     // read_bundled_file
#include "segacd.h"
#include "sms.h"

// The ROM database, compiled in by cmake/blastem.cmake. It is a C file's symbol, so the declaration has to sit inside
// this block or the link finds a mangled name.
extern const char rom_db_data[];

// -- The globals libblastem.c would define ---------------------------------------------------------------
// The config tree, built once by the first system that activates and never written after (every instance reads the
// same settings: the region is a build parameter, not a config value, so no per-instance tree is needed).
tern_node* config = nullptr;
system_header* current_system = nullptr;
// NEVER written. vdp.c reads it at init (it mallocs its own line buffer when set), at free (it frees that buffer) and
// at every frame end (it skips the publish), so a value that changed between those three either leaks the first
// instance's buffer or writes a released one. With it 0, the VDP always asks render_get_framebuffer() for a buffer,
// which is the seam this file and the shell's video sink supply.
int headless = 0;
int exit_after = 0;
int z80_enabled = 1;
// NEVER written. The one caller that opens it as a path (genesis.c's persist_save) is reached only from blastem.c,
// BlastEm's own frontend, which is not compiled, and the battery is the host's to save out of gen->save_storage. So the
// null is dead code rather than a crash.
char* save_filename = nullptr;
uint8_t use_native_states = 1;

static const system_media kNoMedia = {};
const system_media* current_media(void) { return &kNoMedia; }

void process_events(void) {}

// BlastEm's ROM database is compiled in (rom.db.c), as libblastem.c does it. `config.c` is the other caller: it asks
// for "systems.cfg" and "default.cfg" through here, and BOTH ARE DELIBERATELY NULL. A null leaves
// get_systems_config()/get_model() on BlastEm's built-in default model name (`get_model`'s "md1va3"), whose values are
// the ones this console wants: tmss off, fm "discrete 2612", vsram 40, vram 64. Bundling either file would mean a
// second generated C string for no behaviour change.
char* read_bundled_file(char* name, uint32_t* sizeret) {
    if (!std::strcmp(name, "rom.db")) {
        *sizeret = static_cast<uint32_t>(std::strlen(rom_db_data));
        char* ret = static_cast<char*>(std::malloc(*sizeret + 1));
        // Out of memory is a null, which config.c reads as "no bundled file" and romdb.c turns into its "Failed to load
        // ROM DB" message: either is better than a memcpy through null.
        if (!ret) return nullptr;
        std::memcpy(ret, rom_db_data, *sizeret + 1);
        return ret;
    }
    return nullptr;
}

uint8_t get_host_address(iface_info*) { return 0; }  // megawifi.c, only a WiFi cart's mapper calls it
void bindings_reacquire_capture(void) {}
void bindings_release_capture(void) {}
void bindings_set_mouse_mode(uint8_t) {}
void init_terminal(void) {}

// -- The render seam -------------------------------------------------------------------------------
pixel_t render_map_color(uint8_t r, uint8_t g, uint8_t b) {
    return 0xFF000000u | uint32_t(r) << 16 | uint32_t(g) << 8 | b;
}
// The standard the core last asked for. Nothing in this build reads it back (the picture's geometry comes from the VDP's
// own registers and the region from the context), so it exists because the callback has to be defined and its argument
// is worth keeping rather than dropped.
vid_std video_standard = VID_NTSC;
void render_set_video_standard(vid_std standard) { video_standard = standard; }
uint8_t render_create_window(char*, uint32_t, uint32_t, window_close_handler) { return 0; }
void render_destroy_window(uint8_t) {}
// The VDP's frame end. NO frame-end signal: a frontend ends the frame from this callback (render_sdl.c and libblastem.c
// call system_request_exit here), and this host must not, because a block is longer than a frame, so a stop at the frame
// boundary would leave the rest of the block undrained and starve its audio. The stop is run_until's, once a block. The
// sink copies the picture out synchronously, so the VDP may reuse the buffer it just handed over.
void render_framebuffer_updated(uint8_t which, int width) {
    auto* sys = static_cast<rp::blastem::BlastemMdSystem*>(rp::blastem::currentInstance);
    if (!sys) return;  // a context outside any system has no frame to publish, and the VDP does not read one back
    vdp_context* vdp = sys->vdp();
    if (!vdp) return;  // an alloc still in flight has a system but no context to take the picture's mode from
    sys->video().publish(which, width, *vdp);
}
// The buffer the VDP draws into. The instance's own sink answers it once prepared, which is the whole story for a
// booted system; the static stays as the fallback for a context built outside any system (what the link test does),
// because the VDP at init points context->output into whatever this returns and draws through it: with headless 0 the
// VDP asks here at init (init_vdp_context's non-headless branch) and after every published frame, so a null is a write
// through null on the first line. 512 rows is what that branch allocates for itself. ONE static serves every instance
// and every `which` (the VDP's debug indices included), so nothing may build on the contents or the identity of this
// buffer: it is a landing pad for writes.
pixel_t* render_get_framebuffer(uint8_t which, int* pitch) {
    auto* sys = static_cast<rp::blastem::BlastemMdSystem*>(rp::blastem::currentInstance);
    if (sys) {
        pixel_t* fb = sys->video().buffer(which, pitch);
        if (fb) return fb;
    }
    static pixel_t kScratch[512 * LINEBUF_SIZE];
    *pitch = LINEBUF_SIZE * static_cast<int>(sizeof(pixel_t));
    return kScratch;
}
uint8_t render_get_active_framebuffer(void) { return FRAMEBUFFER_ODD; }
uint8_t render_fullscreen(void) { return 0; }
uint32_t render_overscan_top(void) { return 0; }  // the full picture: the sink places it, so no overscan crop
uint32_t render_overscan_bot(void) { return 0; }
uint8_t render_should_release_on_exit(void) { return 0; }
uint8_t render_is_threaded_video(void) { return 0; }
// The audio *backend* half of render_audio.h. BlastEm's own render_audio.c is compiled at first (its source/registry
// and its linear resampler); these are what its backend half needs: no sync, no device, one buffer's worth of
// minimum. The shell's own sink replaces the whole file and these with it.
uint8_t render_is_audio_sync(void) { return 0; }
void* render_new_audio_opaque(void) { return nullptr; }
void render_free_audio_opaque(void*) {}
void render_lock_audio(void) {}
void render_unlock_audio(void) {}
uint32_t render_min_buffered(void) { return 1024; }
uint32_t render_audio_syncs_per_sec(void) { return 0; }
void render_audio_created(audio_source*) {}
void render_do_audio_ready(audio_source*) {}
void render_source_paused(audio_source*, uint8_t) {}
void render_source_resumed(audio_source*) {}
void render_buffer_consumed(audio_source*) {}

// -- The Sega CD (gen->expansion is NULL for a Mega Drive) ------------------------------------------------
segacd_context* alloc_configure_segacd(system_media*, uint32_t, uint8_t, rom_info*) { return nullptr; }
void free_segacd(segacd_context*) {}
void scd_run(segacd_context*, uint32_t) {}
void scd_adjust_cycle(segacd_context*, uint32_t) {}
uint32_t gen_cycle_to_scd(uint32_t, genesis_context*) { return 0; }
void segacd_config_updated(segacd_context*) {}
void segacd_format_bram(uint8_t*, size_t) {}
memmap_chunk* segacd_main_cpu_map(segacd_context*, uint8_t, uint32_t* n) {
    *n = 0;
    return nullptr;
}
void segacd_register_section_handlers(segacd_context*, deserialize_buffer*) {}
void segacd_serialize(segacd_context*, serialize_buffer*, uint8_t) {}
void segacd_set_speed_percent(segacd_context*, uint32_t) {}
uint8_t parse_cue(system_media*) { return 0; }
uint8_t parse_toc(system_media*) { return 0; }
uint32_t make_iso_media(system_media*, const char*) { return 0; }
uint32_t make_chd_media(system_media*, const char*) { return 0; }

// -- The 32X (gen->mars is NULL) --------------------------------------------------------------------------
s32x* alloc_32x(system_media*, uint8_t, uint8_t) { return nullptr; }
void free_32x(s32x*) {}
void s32x_run(s32x*, uint32_t) {}
void s32x_adjust_cycles(s32x*, uint32_t) {}
void s32x_set_speed(s32x*, uint32_t) {}
void s32x_video_run(s32x_video*, uint32_t) {}
uint8_t s32x_video_composite(s32x_video*, pixel_t*, uint8_t*, uint32_t, uint8_t) { return 0; }
void s32x_fb_debug(pixel_t*, uint32_t, s32x_video*) {}
// The bus handlers a 32X cart's memory map would install (32x.h). romdb.c puts them in a chunk's read/write slots for
// the mappers that carry a 32X, so the pointers are taken; a Mega Drive cart's map has none, and the one cart type that
// could ask for one is refused by the ROM gate before a context exists. Writes answer with the context they were handed
// (upstream's own tail), reads with 0.
uint16_t s32x_68k_read(uint32_t, void*) { return 0; }
void* s32x_68k_write(uint32_t, void* vcontext, uint16_t) { return vcontext; }
uint8_t s32x_68k_read_b(uint32_t, void*) { return 0; }
void* s32x_68k_write_b(uint32_t, void* vcontext, uint8_t) { return vcontext; }
void* s32x_write_hint(uint32_t, void* vcontext, uint16_t) { return vcontext; }
void* s32x_write_hint_b(uint32_t, void* vcontext, uint8_t) { return vcontext; }
void* s32x_fb_write_w(uint32_t, void* vcontext, uint16_t) { return vcontext; }
void* s32x_fb_write_b(uint32_t, void* vcontext, uint8_t) { return vcontext; }
uint16_t s32x_fb_read_w(uint32_t, void*) { return 0; }
uint8_t s32x_fb_read_b(uint32_t, void*) { return 0; }
void* s32x_overwrite_write_w(uint32_t, void* vcontext, uint16_t) { return vcontext; }
void* s32x_overwrite_write_b(uint32_t, void* vcontext, uint8_t) { return vcontext; }

// -- The other systems' allocators (no Mega Drive path calls one) ------------------------------------------
coleco_context* alloc_configure_coleco(system_media*) { return nullptr; }
// Four parameters, exactly as sms.h declares them: the prototype's three-parameter stub compiled only while nothing
// included sms.h, which is not a property to rely on.
sms_context* alloc_configure_sms(system_media*, system_type, uint32_t, uint8_t) { return nullptr; }
laseractive* alloc_laseractive(system_media*, uint32_t) { return nullptr; }
media_player* alloc_media_player(system_media*, uint32_t) { return nullptr; }
gen_player* alloc_config_gen_player(void*, uint32_t) { return nullptr; }
gen_player* alloc_config_gen_player_reader(event_reader*) { return nullptr; }

// -- The SMS cart mappers (romdb.c's SMS and Korean-SMS rows; a Mega Drive ROM never reaches either) --------
// The write handlers themselves, declared in romdb.c next to the maps that name them and defined in the sms.c and
// korean_sms_multi.c this build leaves out.
void* sms_sega_mapper_write(uint32_t, void* vcontext, uint8_t) { return vcontext; }
void* sms_cart_ram_write(uint32_t, void* vcontext, uint8_t) { return vcontext; }
void* sms_codemasters_bank0_write(uint32_t, void* vcontext, uint8_t) { return vcontext; }
void* sms_codemasters_bank1_write(uint32_t, void* vcontext, uint8_t) { return vcontext; }
void* sms_codemasters_bank2_write(uint32_t, void* vcontext, uint8_t) { return vcontext; }
void* super_game_30_write(uint32_t, void* vcontext, uint8_t) { return vcontext; }

// -- The Pico-tenant chips (pico_pcm.c and ymz263b.c are not compiled; pico_pcm.h and ymz263b.h are, through
// -- genesis.h, because genesis_context carries the pointers) ---------------------------------------------
void pico_pcm_init(pico_pcm*, uint32_t, uint32_t) {}
void pico_pcm_free(pico_pcm*) {}
void pico_pcm_adjust_master_clock(pico_pcm*, uint32_t) {}
void pico_pcm_run(pico_pcm*, uint32_t) {}
void pico_pcm_ctrl_write(pico_pcm*, uint16_t) {}
void pico_pcm_data_write(pico_pcm*, uint16_t) {}
uint16_t pico_pcm_ctrl_read(pico_pcm*) { return 0; }
uint16_t pico_pcm_data_read(pico_pcm*) { return 0; }
uint32_t pico_pcm_next_int(pico_pcm*) { return 0; }

void ymz263b_init(ymz263b*, uint32_t, uint32_t) {}
void ymz263b_free(ymz263b*) {}
void ymz263b_adjust_master_clock(ymz263b*, uint32_t) {}
void ymz263b_run(ymz263b*, uint32_t) {}
uint32_t ymz263b_next_int(ymz263b*) { return 0; }
void ymz263b_address_write(ymz263b*, uint8_t) {}
void ymz263b_data_write(ymz263b*, uint32_t, uint8_t) {}
uint8_t ymz263b_data_read(ymz263b*, uint32_t) { return 0; }
uint8_t ymz263b_status_read(ymz263b*) { return 0; }

// -- The socket seam (net.c and net_win.c are not compiled; event_log.c and megawifi.c call through it) ------
// Nothing here opens a socket, so the answers are the ones that end a conversation rather than spin: no error, not
// would-block. event_log.c's server and megawifi's link are started only for a network-sourced media or a WiFi cart's
// mapper, and a Mega Drive cart gets neither.
void socket_init(void) {}
int socket_blocking(int, int) { return 0; }
void socket_close(int) {}
int socket_last_error(void) { return 0; }
int socket_error_is_wouldblock(void) { return 0; }
}
