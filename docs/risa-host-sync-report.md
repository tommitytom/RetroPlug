# Risa 2.3.0 host sync: mid-song locate diverges from the song structure

A report from the RetroPlug side, covering what we found integrating the 2.3.0 host-sync receive path.

Short version: the byte protocol works, and the parts of it we could measure behave exactly as
`docs/sync/host-sync-protocol.md` describes. The problem is in the PPQ mapping the doc specifies for
hosts. It can only be correct for a song laid out uniformly, so for a normal song a mid-song locate
lands somewhere other than where the music actually is. Playing from the top is unaffected, which is
why this hides until you stop and start again.

All measurements below are against `risa-2.3.0-pal.nes` running in RetroPlug's Mesen-based NES core,
not on hardware.

---

## 1. What works

Driven from a DAW transport at 48 kHz / 249 BPM / 1024-frame blocks, verified byte by byte against a
capture of the host stream:

| Play | Arm packet | PPQ span | Clocks expected | Clocks sent |
| --- | --- | --- | --- | --- |
| 1 | `F9 52 00 00 00` | 0.000000 to 16.113067 | 386.7 | 386 |
| 2 | `F9 52 00 04 02` | 16.113067 to 19.477333 | 80.7 | 81 |
| 3 | `F9 52 00 04 53` | 19.477333 to 38.689067 | 461.1 | 461 |

One arm and one `FA` per start, `FC` on every stop, no pre-roll clocks, no clock for the armed
position.

On a real core we also confirmed:

- **No clock loss up to at least 260 BPM.** A song hitting every fourth row advanced exactly 4 rows per
  quarter note over 16-beat runs at 100, 120, 150, 180, 200, 220, 240, 249 and 260 BPM: 64 rows of 64
  every time. At 260 BPM the clock period is 9.6 ms, comfortably inside the service budget.
- **The sub-row phase is honoured.** Locates with `tt % 6` of 0, 1, 2, 3 and 5 all resumed in phase.
  Residual offset was about 0.5 of a clock in every case, on-grid or not, consistent with the
  documented subframe service quantization rather than with anything phase-dependent.
- **The locate lands on the named row** after stops, after rewinds, and on jumps to arbitrary
  positions.

One observation for anyone else measuring this: applying a locate is visibly two-stage. For a few ms
after `FA` the row still reads 0, and only then does the subframe path load `floor(tt/6)`. Sampling the
first block in which playback is active reports row 0 for every locate. We had to sample for the row
that persists.

---

## 2. The problem

The doc gives hosts this mapping:

```text
absoluteClock = floor(max(ppq, 0) * 24)
phrase        = floor(absoluteClock / 96)
songRow       = (phrase >> 4) & 0x7f
chainRow      = phrase & 0x0f
tickOffset    = absoluteClock % 96
```

This assumes every song row contains 16 phrases, every phrase 16 rows, and every row 6 ticks. Playback
does not work that way: when a chain runs out of populated rows, risa advances the song row. So the
grid and the music diverge as soon as the first chain wraps.

Measured with one song played three times, identical except for how many chain rows are populated
(120 BPM, position sampled once per bar):

```text
chain rows used = 16    bar 4: grid says chain 4    risa at song 0 chain 4     agrees
                        bar 5: grid says chain 5    risa at song 0 chain 5     agrees

chain rows used = 4     bar 4: grid says chain 4    risa at song 0 chain 0     wrapped
                        bar 5: grid says chain 5    risa at song 1 chain 1     diverged
                        bar 6: grid says chain 6    risa at song 1 chain 2

chain rows used = 2     bar 2: grid says chain 2    risa at song 0 chain 0     wrapped
                        bar 3: grid says chain 3    risa at song 1 chain 1
                        bar 4: grid says chain 4    risa at song 1 chain 0
```

Only the fully populated song tracks the grid. That is the artificial case; the other two are what
songs normally look like.

Groove is a second instance of the same assumption. A row lasts `groove` ticks, not necessarily 6, so
`floor(tt / 6)` names the wrong row for any song not on a 6-tick groove. The doc already notes that
custom groove commands do not change the six-clock locate grid, so this is understood, but it
compounds with the structural divergence above.

### Why it surfaces as a stop/start fault

- **Playing from the top is correct.** The arm names song row 0, chain row 0, row 0, which genuinely is
  where the song starts. From there risa follows its own structure and the host's grid drifts away from
  it, but nothing acts on that drift, so it is inaudible. The song simply plays, locked to the host
  clock.
- **The next start is wrong.** The arm now imposes an absolute position derived from a PPQ that no
  longer corresponds to the same musical point, and risa is pulled there. Tempo stays locked, so it
  does not read as a timing fault; the song has jumped somewhere else in the arrangement.

The user report that started this was "I press play, it's fine; I stop and hit play again and it goes
out of sync", with the sync itself described as hard to pin down. That is this.

It is worth noting that a host cannot avoid this by being careful. The mapping is the only one the
protocol defines, and the arm packet's `ss`/`cc`/`tt` fields are the only way to express a position.
Sending the correct position instead would require the host to model risa's structure and flow control:
chain and song advance, sparse-row fallback, per-row grooves, hops. That is a reimplementation of the
sequencer, coupled to a ROM that changes between versions. We do have the song data available and the
packet is expressive enough to name any row and sub-row phase, so it is *possible* on our side; it just
seems like the wrong side of the seam.

---

## 3. What would help

Roughly in order of how much they would buy relative to the work:

**A `FB` Continue message.** MIDI System Real-Time already has `F8` clock, `FA` start, `FB` continue and
`FC` stop; the protocol currently uses three of the four. `FB` means precisely "resume from where you
are, without relocating". Since `seq_stop` preserves the playhead (it clears voice and effect state but
does not touch `bss_song_row`, `bss_chain_row`, `zp_phrase_row`, `bss_chain_idx` or `bss_phrase_idx`),
the state needed to honour it is already intact after a stop. This alone fixes the common case: a host
that stops and resumes in place would never need to locate at all, so it could never locate wrongly.

**A position readback.** A WRAM status block exposing the current song row, chain row, phrase row and
sub-row phase would let a host resume exactly, and verify or correct a locate after the fact instead of
assuming it landed. This also gives a host a way to detect the divergence above rather than being blind
to it.

**Locate by absolute tick.** If the arm could carry "tick T since song start" and risa resolved it
against its own structure, the mapping would live where the structure and the sequencer semantics
already are. This is the general fix for a genuine seek: dragging the playhead, a loop jump, play from
bar N. It is also the most work, and there are real questions about whether it is even well defined for
a song with hops, so the readback may be the better value.

**Or document the constraint.** If mid-song locate is only intended to be exact for a uniform song,
saying so plainly would let hosts warn users and let users lay songs out accordingly. That is a
perfectly reasonable resolution; we would rather know than guess.

---

## 4. What we are doing on our side

Fixing the resume case, since that is a host policy choice and does not need anything from the ROM: we
should not be re-locating at all when the transport resumes at the position it stopped at. The
playhead survives the stop, so the correct behaviour is to leave it alone.

The general seek is the part we are raising here.
