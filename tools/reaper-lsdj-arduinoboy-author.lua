-- reaper-lsdj-arduinoboy-author.lua
--
-- Build the Arduinoboy startup-sync test project. Two tracks:
--   1. RetroPlug VST3i (panned hard-L) loaded with LSDj configured for
--      SYNC=LSDJ via the autoload .rplg. MIDI item sends note 24
--      (Arduinoboy play-enable) at t=0 and note 25 (play-disable) just
--      before the end of the render window.
--   2. ReaSynth (panned hard-R) plays a short pulse on every quarter
--      note at 120 BPM. Click[0] at t=0 is the reference for measuring
--      how long it takes LSDj to start producing audio after the host
--      transport begins.
--
-- The render output is stereo: LSDj on left, click on right. Pan
-- isolation lets tools/reaper-timing-analyze.py detect the first onset
-- in each channel and report the startup latency. Per-beat sync drift
-- would require an envelope-edit in the bootstrap script to surface
-- repeated transients (default LSDj instrument 00 sustains the first
-- row's note across the whole phrase).

local dest = os.getenv("REAPER_AUTHOR_DEST") or "/tmp/lsdj_arduinoboy_metro.rpp"
local render_dir = os.getenv("REAPER_AUTHOR_RENDER_DIR") or ""

local logf = io.open("/tmp/reaper-author-script.log", "w")
local function log(msg)
    if logf then logf:write(msg .. "\n"); logf:flush() end
    reaper.ShowConsoleMsg(msg .. "\n")
end
log("[author] start, dest=" .. dest .. " render_dir=" .. render_dir)

local DURATION = 8.0   -- seconds; 4 bars at 120 BPM
local BPM      = 120

-- New project, set tempo
reaper.Main_OnCommand(40023, 0)  -- File: New project
reaper.SetCurrentBPM(0, BPM, false)

-- Track 1: RetroPlug (LSDj via autoload), panned hard-left
reaper.InsertTrackAtIndex(0, true)
local t_rp = reaper.GetTrack(0, 0)
reaper.GetSetMediaTrackInfo_String(t_rp, "P_NAME", "RetroPlug", true)
reaper.SetMediaTrackInfo_Value(t_rp, "D_PAN", -1.0)

local fx_rp = reaper.TrackFX_AddByName(t_rp, "VST3i:RetroPlug", false, -1)
if fx_rp < 0 then
    log("ERROR: RetroPlug VST3i not found")
    return
end
log("[author] RetroPlug FX at index " .. fx_rp)

-- MIDI item on RetroPlug: note 24 enables Arduinoboy clock emission,
-- note 25 disables. Per LsdjSyncRole::handleArduinoboyInput().
local it_rp = reaper.CreateNewMIDIItemInProj(t_rp, 0, DURATION, false)
local tk_rp = reaper.GetMediaItemTake(it_rp, 0)
local function ppq_rp(t) return reaper.MIDI_GetPPQPosFromProjTime(tk_rp, t) end
-- Play-enable: very short note at t=0 (LSDj only cares about the NoteOn)
reaper.MIDI_InsertNote(tk_rp, false, false, ppq_rp(0), ppq_rp(0.05),
                       0, 24, 100, false)
-- Play-disable: short note near the end so render captures a clean stop
reaper.MIDI_InsertNote(tk_rp, false, false,
                       ppq_rp(DURATION - 0.1), ppq_rp(DURATION - 0.05),
                       0, 25, 100, false)
reaper.MIDI_Sort(tk_rp)

-- Track 2: Click, ReaSynth, panned hard-right
reaper.InsertTrackAtIndex(1, true)
local t_ck = reaper.GetTrack(0, 1)
reaper.GetSetMediaTrackInfo_String(t_ck, "P_NAME", "Click", true)
reaper.SetMediaTrackInfo_Value(t_ck, "D_PAN", 1.0)

-- ReaSynth ships with Reaper; the FX name matches the entry the
-- discovery scan writes to reaper-vstplugins64.ini.
local fx_ck = reaper.TrackFX_AddByName(t_ck, "ReaSynth", false, -1)
if fx_ck < 0 then
    log("ERROR: ReaSynth not found")
    return
end
log("[author] ReaSynth FX at index " .. fx_ck)

-- MIDI item on Click: one short C-5 note per quarter beat. At 120 BPM
-- that's a click every 0.5s — 16 clicks over an 8s render. Note length
-- 0.05s so each click is a sharp transient (good for peak detection).
local it_ck = reaper.CreateNewMIDIItemInProj(t_ck, 0, DURATION, false)
local tk_ck = reaper.GetMediaItemTake(it_ck, 0)
local function ppq_ck(t) return reaper.MIDI_GetPPQPosFromProjTime(tk_ck, t) end
local BEAT_SEC = 60.0 / BPM
local n_beats = math.floor(DURATION / BEAT_SEC + 0.5)
for i = 0, n_beats - 1 do
    local t_on = i * BEAT_SEC
    reaper.MIDI_InsertNote(tk_ck, false, false,
                           ppq_ck(t_on), ppq_ck(t_on + 0.05),
                           0, 72, 100, false)  -- C-5
end
reaper.MIDI_Sort(tk_ck)
log("[author] inserted " .. n_beats .. " click notes")

-- Render settings.
if render_dir ~= "" then
    reaper.GetSetProjectInfo_String(0, "RENDER_FILE", render_dir, true)
    reaper.GetSetProjectInfo_String(0, "RENDER_PATTERN",
        "reaper-arduinoboy-metro", true)
end
reaper.GetSetProjectInfo(0, "RENDER_SETTINGS", 0, true)        -- master mix
reaper.GetSetProjectInfo(0, "RENDER_BOUNDSFLAG", 1, true)      -- entire project
reaper.GetSetProjectInfo(0, "RENDER_CHANNELS", 2, true)
reaper.GetSetProjectInfo(0, "RENDER_SRATE", 44100, true)
reaper.GetSetProjectInfo_String(0, "RENDER_FORMAT",
    "ZXZhdxgAAAA=", true)  -- WAV + 16-bit defaults

reaper.Main_SaveProjectEx(0, dest, 0)
log("[author] saved " .. dest)

reaper.Main_OnCommand(40004, 0)  -- File: Quit REAPER
