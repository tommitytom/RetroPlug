-- reaper-mgb-timing-author.lua
--
-- Author the mGB MIDI-timing test project. Proves host MIDI-in keeps its intra-block sample
-- offset through a real DAW render (the fix that stopped SameBoy collapsing every event to
-- frame 0). Rendered with a LARGE audio block (REAPER_JACK_PERIOD=8192 → the plugin's
-- run(frames,…) sees 8192-frame blocks), so an offset within a block is tens of ms — resolvable
-- in the rendered audio.
--
-- Two-track shape, same as the LSDj timing fixtures: track 1 = RetroPlug/mGB (pan hard-L),
-- track 2 = ReaSynth click (pan hard-R) → the stereo analyzer reads mGB on L and the reference on R.
--
-- Placement (SR 44100, block 8192): TWO mGB notes are dropped into the SAME render block K, one
-- near the block start and one near its end, so their onset spacing in the audio is the delta of
-- their frame offsets. A ReaSynth click coincides with the LATE mGB note, giving an independent
-- absolute-position reference at the same instant.
--
--   honoured  → two mGB onsets ~136 ms apart; the late one aligned with the click.
--   collapsed → both mGB notes fire at the block start (one merged onset, ~136 ms BEFORE the click).
--
-- The mGB project itself loads from the autoloaded .rplg (RETROPLUG_AUTOLOAD_PROJECT); the .rpp
-- stays a template that the render re-applies the fixture into.

local dest       = os.getenv("REAPER_AUTHOR_DEST") or "/tmp/mgb_midi_timing.rpp"
local render_dir = os.getenv("REAPER_AUTHOR_RENDER_DIR") or ""

local logf = io.open("/tmp/reaper-author-script.log", "w")
local function log(msg)
    if logf then logf:write(msg .. "\n"); logf:flush() end
    reaper.ShowConsoleMsg(msg .. "\n")
end

-- Block grid: boundaries fall at K*PERIOD samples from project time 0 (empirically confirmed).
-- Keep both notes ≥1000 samples inside block BLOCK_K so a small grid wobble can't split them across
-- two blocks (which would make even the buggy frame-0 code space them ~1 block apart — a false pass).
local SR       = 44100
local PERIOD   = 8192            -- must match REAPER_JACK_PERIOD for this fixture
local BLOCK_K  = 16              -- the render block both notes live in (~2.97 s in). Placed AFTER the
                                 -- mGB DMG boot (~2.5 s of logo scroll + chime) so the boot burst
                                 -- can't contaminate onset detection and mGB is listening on serial.
local OFF_A    = 1000            -- note A: near the block start
local OFF_B    = 7000            -- note B: near the block end
local NOTE_DUR = 0.03            -- short blips → the envelope dips between them (clean rising edges)
local DURATION = 4.0

local sampA = BLOCK_K * PERIOD + OFF_A
local sampB = BLOCK_K * PERIOD + OFF_B
local tA    = sampA / SR
local tB    = sampB / SR
log(string.format("[author] block K=%d period=%d  A=samp%d(%.4fs) B=samp%d(%.4fs) gap=%d samp",
    BLOCK_K, PERIOD, sampA, tA, sampB, tB, sampB - sampA))

reaper.Main_OnCommand(40023, 0)  -- File: New project
reaper.SetCurrentBPM(0, 120, false)

-- Track 1: RetroPlug (mGB), panned hard-left. Two notes in block K, different pitches so the second
-- is a clean retrigger.
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
reaper.MIDI_InsertNote(tk_rp, false, false, ppq_rp(tA), ppq_rp(tA + NOTE_DUR), 0, 60, 100, false)
reaper.MIDI_InsertNote(tk_rp, false, false, ppq_rp(tB), ppq_rp(tB + NOTE_DUR), 0, 67, 100, false)
reaper.MIDI_Sort(tk_rp)

-- Track 2: ReaSynth click, panned hard-right — ONE note coincident with the late mGB note (tB),
-- an independent same-instant absolute reference.
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
    reaper.GetSetProjectInfo_String(0, "RENDER_PATTERN", "reaper-mgb-midi-timing", true)
end
reaper.GetSetProjectInfo(0, "RENDER_SETTINGS", 0, true)
reaper.GetSetProjectInfo(0, "RENDER_BOUNDSFLAG", 1, true)
reaper.GetSetProjectInfo(0, "RENDER_CHANNELS", 2, true)
reaper.GetSetProjectInfo(0, "RENDER_SRATE", SR, true)
reaper.GetSetProjectInfo_String(0, "RENDER_FORMAT", "ZXZhdxgAAAA=", true)

reaper.Main_SaveProjectEx(0, dest, 0)
log("[author] saved " .. dest)

reaper.Main_OnCommand(40004, 0)  -- File: Quit REAPER
