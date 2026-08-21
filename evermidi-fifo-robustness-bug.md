# EverMIDI: FIFO MIDI parser hangs after a stray/misaligned byte (does not resync on status bytes)

Context: driving the EverMIDI ROM on a real NES + Everdrive N8 Pro by writing raw MIDI
bytes into the N8 cart FIFO (device addr `0x1810000`, which the ROM reads at `$40F0/$40F1`).
Two independent drivers were used: the RetroPlug host (`retroplug-cli n8-bridge`, over the
kernel CDC-ACM) and a Raspberry Pi Pico USB-host bridge. Same symptom from both.

## What is PROVEN (observed on hardware, via the N8 APU write-mirror "sniffer")

The sniffer (`memRD 0x1802000`) is the N8's live mirror of the ROM's `$4000-$401F` APU
register writes - i.e. ground truth for what EverMIDI actually drove, independent of audio.

1. **A freshly booted EverMIDI plays arbitrary notes correctly.** Injecting single
   note-ons moved Pulse1's timer to exactly the right value every time:
   - `90 45 7f` (A4, note 69) -> Pulse1 timer 235
   - `90 48 7f` (C5, note 72) -> timer ~198
   - `90 37 7f` (G3, note 55) -> timer ~528
   So the FIFO read path, the MIDI parse, and the APU output are all correct in a good state.

2. **After a USB host disconnect/reconnect, EverMIDI gets stuck and never recovers.**
   It holds the *last note's* pitch forever and ignores every subsequent MIDI message, even
   though:
   - the new messages are correctly framed MIDI (verified byte-for-byte on the wire), and
   - the writes are delivered successfully (the N8's Edio accepts them; the write side
     reports success), and
   - the sniffer confirms Pulse1's timer is pinned at the last note (e.g. 397 = C4, or
     529 = G3) and never updates again.

3. **Clean, slow injection does not recover it.** Sending one note-on per second (no
   aftertouch, no bursts) after the wedge - Pulse1's timer never moves off the stuck value.
   So it is not a transient FIFO overflow that drains; it is a persistent state.

4. **A full power-cycle recovers it** to the fresh, working state in (1).

The trigger in our setup was a USB host handoff (moving the N8's USB cable from one host to
another, which forces a CDC re-enumeration). We also saw it after flooding the FIFO with
continuous channel-pressure (aftertouch) from a Launchpad, but that may have been
coincidental with a handoff; the handoff is the reliable trigger.

## Most likely cause (HYPOTHESIS)

A byte-framing desync in EverMIDI's FIFO MIDI parser. The USB reconnect (or a FIFO
overflow) leaves one or more stray/partial bytes in the cart FIFO, shifting the byte
boundary. From then on EverMIDI reads every 3-byte MIDI message off-by-N and never
realigns - the classic MIDI "off-by-one framing" bug - because it does **not resync on
status bytes**.

A robust MIDI parser treats any byte with the high bit set (`>= 0x80`) as a new **status
byte** and restarts its message state right there, which auto-heals from a stray byte
within a single message. The symptom (permanent wedge from a single misaligned byte, only
cleared by reset) is exactly what you get when the parser instead consumes fixed 3-byte
groups (or otherwise never treats a mid-stream `>= 0x80` byte as a fresh status).

Alternative to rule out: a hard CPU hang on the reconnect rather than a parser desync.
Against this: the APU mirror keeps showing a sustained note, suggesting the per-frame APU
refresh loop is still running (alive but mis-parsing), not halted. The repro below
distinguishes the two.

## Recommended fix (in the EverMIDI ROM, `../evermidi`)

Make the FIFO MIDI reader resync on status bytes (standard MIDI parsing rules):

- On each byte read from the FIFO:
  - if `byte >= 0xF8` (System Real-Time): handle/ignore it **without** disturbing any
    in-progress message (these can interleave anywhere).
  - else if `byte >= 0x80` (Status): **start a new message** - latch it as the current
    status (also the running-status byte for channel-voice), reset the data-byte count.
    Do this regardless of where the previous message was, so a mid-stream status byte
    always realigns the parser.
  - else (Data, `< 0x80`): append to the current message's data bytes. If there is no
    valid current/running status yet, **discard** it. Emit the message once it has its
    required number of data bytes (2 for note on/off/CC/pitch-bend/poly-AT, 1 for
    program-change/channel-pressure); support running status (another data byte after a
    complete channel-voice message re-uses the last status).

This makes EverMIDI self-heal from any stray/misaligned/dropped byte within one message,
instead of wedging forever.

Secondary hardening (optional): if the FIFO can overflow and silently drop bytes, the
resync-on-status rule already covers the fallout. If there is a way to detect a FIFO
overflow/underflow flag and flush, that's a nice extra, but the status-byte resync is the
core fix.

## Repro the other agent can run (no special hardware handoff needed)

With EverMIDI booted and playing:
1. Write a **single stray data byte** into the FIFO with no status (e.g. one `0x00` or
   `0x7f`), then write a valid note-on (`90 3c 7f`).
2. A correct (resync-on-status) parser plays C4 - it realigns on the `0x90` status byte.
   The current build is expected to instead mis-frame and either play the wrong note or
   wedge, confirming the off-by-one framing bug.
3. If step 1 alone (a single stray byte, no reconnect) can wedge it, that confirms the
   parser-desync hypothesis over a CPU-hang-on-reconnect.

## Bridge side (for reference - NOT the bug)

The bridge that writes to the FIFO is correct and healthy: byte-exact Edio framing,
one `fifoWR` (= `memWR(0x1810000, midiBytes)`) per complete MIDI message, writes confirmed
delivered, sniffer reads working throughout. The bridge already drops MIDI clock / sensing
/ transport and now also drops aftertouch (poly `0xA0` + channel-pressure `0xD0`) to avoid
flooding the FIFO. The wedge happens regardless, on correctly-framed input - so the fix
belongs in the ROM's parser.
