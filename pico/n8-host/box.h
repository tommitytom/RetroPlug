// The parts that make the bridge a standalone box rather than a dev rig: the status LED, the
// watchdog and recovery reboots, the ROMs staged in the Pico's flash, and which of them to boot.
#ifndef BOX_H
#define BOX_H

#include <stdbool.h>
#include <stdint.h>

// --- Status LED (the Pico's own, GP25) -----------------------------------------------------
typedef enum {
    LED_NO_N8,   // no N8 on the socket (or the NES is off): a short blip once a second
    LED_BUSY,    // N8 up, not forwarding yet (probe, menu, upload, boot): 4 Hz blink
    LED_READY,   // forwarding: solid, dropping out briefly on each forwarded batch
    LED_ERROR,   // the boot failed, or recovery is backing off: a double blink
} led_mode;

void led_init(void);
void led_set_mode(led_mode m);
void led_activity(void);      // MIDI went out: a short flicker in LED_READY
void led_count(unsigned n);   // acknowledge with n blinks, then back to the mode

// --- Watchdog + recovery ----------------------------------------------------------------------
// The watchdog reboots a Pico that stops feeding it (a hang). box_recover() reboots on purpose,
// when the link is wedged in a way a fresh USB host clears. Both are remembered across the reboot
// (watchdog scratch 0-2; the SDK owns 4-7), and consecutive recoveries back off - 5, 10, 20, 40,
// 60 s - so a fault the box cannot clear (the N8 itself wedged) is not a reboot loop.
enum {
    RECOVER_HANG = 1,       // the watchdog fired: nothing fed it for 3 s
    RECOVER_NO_ENUM,        // a device on the socket that never enumerated
    RECOVER_LINK_DEAD,      // the N8 stopped answering CMD_STATUS
    RECOVER_WRITES_FAIL,    // fifoWRs kept failing
    RECOVER_TEST,           // a test hook asked for it
};

void box_start(void);                 // report why we booted, then arm the watchdog
void box_feed(void);                  // feed the watchdog + run the LED; also the edio idle hook
bool box_may_recover(void);           // false while backing off after recent recoveries
void box_recover(int reason);         // reboot now; never returns
void box_note_healthy(void);          // the link works: 60 s of it clears the backoff

// --- ROMs staged in the Pico's flash ------------------------------------------------------------
// `loop.sh --rom a.nes --rom b.nes ...` writes them back to back at ROM_TABLE_ADDR, each a 64-byte
// header + the ROM, padded to the next 4 KB. The table ends at the first slot without the magic.
typedef struct {
    char     magic[4];   // "N8RM"
    uint32_t size;       // ROM bytes following the header
    char     path[56];   // its destination on the SD card, NUL-terminated
} rom_blob_hdr;

int                 rom_count(void);
const rom_blob_hdr *rom_get(int i);
uint32_t            rom_crc32(const rom_blob_hdr *h);

// The ROM picked over MIDI, waiting for the next menu to boot it (-1 = none). Kept in watchdog
// scratch too, so a recovery reboot between the pick and the menu does not lose it.
int  box_pending_sel(void);
void box_set_pending_sel(int sel);

// What the box last booted, saved on the N8's SD card - so the choice survives the box losing
// power, and a ROM already uploaded is not uploaded again (its CRC is remembered).
#define BOX_STATE_PATH "usb-games/pico/box.cfg"
typedef struct {
    char     magic[4];   // "N8PB"
    uint8_t  sel;        // index into the ROM table
    uint8_t  pad[3];
    uint32_t crc;        // CRC-32 of the ROM last uploaded
    uint32_t reserved;
} box_state;             // 16 bytes on the card

bool box_state_load(box_state *s);
bool box_state_save(const box_state *s);

#endif // BOX_H
