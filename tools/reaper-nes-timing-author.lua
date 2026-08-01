-- reaper-nes-timing-author.lua
--
-- Author the NES (n8-midi) MIDI-timing test project — the NES counterpart of reaper-mgb-timing-author.lua.
-- Proves host MIDI-in keeps its intra-block sample offset through a real DAW render (the fix that stopped
-- NesN8MidiRole collapsing every event to the block start). Rendered with a LARGE block
-- (REAPER_JACK_PERIOD=8192), so an offset within a block is tens of ms — resolvable in the audio.
--
-- Two-track shape: track 1 = RetroPlug/NES (pan hard-L), track 2 = ReaSynth click (pan hard-R).
--
-- NES specifics vs mGB:
--   * drive MIDI channel 1 only (ch1 -> APU Pulse1; ch2 of n8-midi is broken).
--   * a PRIMING note first — n8-midi drops its very first MIDI message; place it well before the measured
--     window (and before the analyzer's boot guard) so the two measured notes are #2 and #3.
--   * n8-midi boots in ~1 s (vs the DMG's ~2.5 s), so the measured window can start earlier.
-- Two measured notes go in the SAME render block K (near its start + end) so their onset spacing is the
-- delta of their frame offsets; a ReaSynth click coincides with the LATE note as an absolute reference.
--   honoured  -> two NES onsets ~136 ms apart; the late one aligned with the click.
--   collapsed -> both fire at the block start (one merged onset, ~136 ms BEFORE the click).

local dest       = os.getenv("REAPER_AUTHOR_DEST") or "/tmp/nes_midi_timing.rpp"
local render_dir = os.getenv("REAPER_AUTHOR_RENDER_DIR") or ""

local logf = io.open("/tmp/reaper-author-script.log", "w")
local function log(msg)
    if logf then logf:write(msg .. "\n"); logf:flush() end
    reaper.ShowConsoleMsg(msg .. "\n")
end

-- Block grid: boundaries at K*PERIOD samples from project time 0 (empirically confirmed on mGB). Keep both
-- measured notes >=1000 samples inside block BLOCK_K so a small grid wobble can't split them across blocks.
local SR        = 44100
local PERIOD    = 8192            -- must match REAPER_JACK_PERIOD for this fixture
local BLOCK_K   = 13              -- the render block both measured notes live in (~2.41 s in; after boot)
local OFF_A     = 1000            -- note A: near the block start
local OFF_B     = 7000            -- note B: near the block end
local NOTE_DUR  = 0.03            -- short blips -> the envelope dips between them (clean rising edges)
local PRIME_T   = 1.2            -- throwaway note (n8-midi eats the first MIDI message); before the boot guard
local DURATION  = 3.2

local sampA = BLOCK_K * PERIOD + OFF_A
local sampB = BLOCK_K * PERIOD + OFF_B
local tA    = sampA / SR
local tB    = sampB / SR
log(string.format("[author] block K=%d period=%d  prime=%.3fs A=samp%d(%.4fs) B=samp%d(%.4fs) gap=%d samp",
    BLOCK_K, PERIOD, PRIME_T, sampA, tA, sampB, tB, sampB - sampA))

reaper.Main_OnCommand(40023, 0)  -- File: New project
reaper.SetCurrentBPM(0, 120, false)

-- Track 1: RetroPlug (NES), panned hard-left. Channel 1 only; a prime note, then the two measured notes.
reaper.InsertTrackAtIndex(0, true)
local t_rp = reaper.GetTrack(0, 0)
reaper.GetSetMediaTrackInfo_String(t_rp, "P_NAME", "RetroPlug", true)
reaper.SetMediaTrackInfo_Value(t_rp, "D_PAN", -1.0)

local fx_rp = reaper.TrackFX_AddByName(t_rp, "VST3i:RetroPlug", false, -1)
if fx_rp < 0 then
    log("ERROR: RetroPlug VST3i not found on plugin path")
    return
end
log("[author] RetroPlug FX at index " .. fx_rp)

local it_rp = reaper.CreateNewMIDIItemInProj(t_rp, 0, DURATION, false)
local tk_rp = reaper.GetMediaItemTake(it_rp, 0)
local function ppq_rp(t) return reaper.MIDI_GetPPQPosFromProjTime(tk_rp, t) end
-- prime (dropped by the ROM) + note A + note B, all ch1 (chan arg 0). Different pitches so B is a clean
-- retrigger of Pulse1.
reaper.MIDI_InsertNote(tk_rp, false, false, ppq_rp(PRIME_T), ppq_rp(PRIME_T + NOTE_DUR), 0, 60, 100, false)
reaper.MIDI_InsertNote(tk_rp, false, false, ppq_rp(tA), ppq_rp(tA + NOTE_DUR), 0, 60, 100, false)
reaper.MIDI_InsertNote(tk_rp, false, false, ppq_rp(tB), ppq_rp(tB + NOTE_DUR), 0, 67, 100, false)
reaper.MIDI_Sort(tk_rp)

-- Track 2: ReaSynth click, panned hard-right — ONE note coincident with the late NES note (tB).
reaper.InsertTrackAtIndex(1, true)
local t_ck = reaper.GetTrack(0, 1)
reaper.GetSetMediaTrackInfo_String(t_ck, "P_NAME", "Click", true)
reaper.SetMediaTrackInfo_Value(t_ck, "D_PAN", 1.0)

local fx_ck = reaper.TrackFX_AddByName(t_ck, "ReaSynth", false, -1)
if fx_ck < 0 then
    log("ERROR: ReaSynth not found")
    return
end

local it_ck = reaper.CreateNewMIDIItemInProj(t_ck, 0, DURATION, false)
local tk_ck = reaper.GetMediaItemTake(it_ck, 0)
local function ppq_ck(t) return reaper.MIDI_GetPPQPosFromProjTime(tk_ck, t) end
reaper.MIDI_InsertNote(tk_ck, false, false, ppq_ck(tB), ppq_ck(tB + NOTE_DUR), 0, 72, 100, false)
reaper.MIDI_Sort(tk_ck)

if render_dir ~= "" then
    reaper.GetSetProjectInfo_String(0, "RENDER_FILE", render_dir, true)
    reaper.GetSetProjectInfo_String(0, "RENDER_PATTERN", "reaper-nes-midi-timing", true)
end
reaper.GetSetProjectInfo(0, "RENDER_SETTINGS", 0, true)
reaper.GetSetProjectInfo(0, "RENDER_BOUNDSFLAG", 1, true)
reaper.GetSetProjectInfo(0, "RENDER_CHANNELS", 2, true)
reaper.GetSetProjectInfo(0, "RENDER_SRATE", SR, true)
reaper.GetSetProjectInfo_String(0, "RENDER_FORMAT", "ZXZhdxgAAAA=", true)

reaper.Main_SaveProjectEx(0, dest, 0)
log("[author] saved " .. dest)

reaper.Main_OnCommand(40004, 0)  -- File: Quit REAPER
