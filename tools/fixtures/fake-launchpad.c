// A fake Launchpad on the ALSA sequencer, for tools/run-sdl-controller.sh: one duplex port named like a Pro
// MK3's USB MIDI interface. It answers a Universal Device Inquiry the way the real device does, answers the
// Pro MK3's "which layout?" query in the Pro's own header (what tells it from a Mini MK3, which shares the
// inquiry bytes), and prints every message it receives as hex, one per line, flushed.
//
// Its CLIENT name is deliberately one no real device has ("RPFakeLPProMK3"), while its PORT keeps the USB
// interface's name ("LPProMK3 MIDI") because that is what a scan's port-hint matching looks for. The client name is
// what makes the harness's allow list safe: RtMidi names an ALSA port "<client>:<port> <client#>:<port#>", so an
// entry naming this client cannot also name a real Pro MK3, whose client is its own device name. What the port stem
// logic sees is therefore "RPFakeLPProMK3:LPProMK3 MIDI": a prefix match on "LPProMK3 MIDI" does NOT hold, so the
// fake is not offered as the default port and the harness reaches it through a scan (which matches on the inquiry
// reply, not the name).
//
// Arguments, any order:
//   chatter     also play a NoteOn every 50 ms, as a pad being pressed would - so a test can see whether those
//               reach the host's shared MIDI stream (they must not while the port is claimed)
//   client=NAME the ALSA client name (default "RPFakeLPProMK3")
//   name=NAME   the PORT name (default "LPProMK3 MIDI")
//   note=N      the note chatter plays (decimal, default 81)
//   noreply     answer nothing - an ordinary keyboard, not a Launchpad
//
// Build: cc -o fake fake-launchpad.c -lasound
#include <alsa/asoundlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
static volatile int run = 1;
static void stop(int s) { (void)s; run = 0; }
static void sendBytes(snd_seq_t* seq, int port, unsigned char* data, long n) {
    snd_midi_event_t* enc;
    snd_midi_event_new(256, &enc);
    snd_seq_event_t ev;
    snd_seq_ev_clear(&ev);
    snd_seq_ev_set_source(&ev, port); snd_seq_ev_set_subs(&ev); snd_seq_ev_set_direct(&ev);
    if (data[0] == 0xF0) snd_seq_ev_set_sysex(&ev, n, data);
    else snd_midi_event_encode(enc, data, n, &ev);
    snd_seq_event_output_direct(seq, &ev);
    snd_midi_event_free(enc);
}
int main(int argc, char** argv) {
    int chatter = 0, noreply = 0, noteNum = 0x51;
    const char* client = "RPFakeLPProMK3";
    const char* name = "LPProMK3 MIDI";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "chatter")) chatter = 1;
        else if (!strcmp(argv[i], "noreply")) noreply = 1;
        else if (!strncmp(argv[i], "client=", 7)) client = argv[i] + 7;
        else if (!strncmp(argv[i], "name=", 5)) name = argv[i] + 5;
        else if (!strncmp(argv[i], "note=", 5)) noteNum = atoi(argv[i] + 5) & 0x7F;
    }
    snd_seq_t* seq;
    // Non-blocking, or the drain loop below blocks waiting for the NEXT event once the queue is empty: then
    // neither a SIGTERM (it would never re-check `run`) nor chatter (it never reaches its timeout) gets a turn.
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK) < 0) return 1;
    snd_seq_set_client_name(seq, client);
    int port = snd_seq_create_simple_port(seq, name,
        SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ | SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    signal(SIGTERM, stop); signal(SIGINT, stop);
    snd_midi_event_t* dec; snd_midi_event_new(4096, &dec); snd_midi_event_no_status(dec, 1);
    printf("ready %d:%d\n", snd_seq_client_id(seq), port); fflush(stdout);
    int npfd = snd_seq_poll_descriptors_count(seq, POLLIN);
    struct pollfd pfd[4]; snd_seq_poll_descriptors(seq, pfd, npfd, POLLIN);
    while (run) {
        if (poll(pfd, npfd, chatter ? 50 : 200) <= 0) {
            if (chatter) { unsigned char note[] = {0x90, (unsigned char)noteNum, 0x7F}; sendBytes(seq, port, note, 3); }
            continue;
        }
        snd_seq_event_t* ev;
        while (snd_seq_event_input(seq, &ev) >= 0) {
            unsigned char buf[4096]; long n = snd_midi_event_decode(dec, buf, sizeof buf, ev);
            if (n <= 0) continue;
            for (long i = 0; i < n; i++) printf("%02x%s", buf[i], i + 1 < n ? " " : "\n");
            fflush(stdout);
            static const unsigned char inq[] = {0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7};
            static const unsigned char layoutQuery[] = {0xF0, 0x00, 0x20, 0x29, 0x02, 0x0E, 0x00, 0xF7};
            if (noreply) continue;
            if (n == 6 && !memcmp(buf, inq, 6)) {
                unsigned char reply[] = {0xF0,0x7E,0x00,0x06,0x02,0x00,0x20,0x29,0x13,0x01,0,0,0,1,2,3,0xF7};
                sendBytes(seq, port, reply, sizeof reply);
            } else if (n == 8 && !memcmp(buf, layoutQuery, 8)) {
                unsigned char reply[] = {0xF0,0x00,0x20,0x29,0x02,0x0E,0x00,0x00,0x00,0xF7};  // layout 0, page 0
                sendBytes(seq, port, reply, sizeof reply);
            }
        }
    }
    snd_seq_close(seq);
    return 0;
}
