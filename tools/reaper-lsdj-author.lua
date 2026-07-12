-- reaper-lsdj-author.lua
--
-- Author an LSDj DAW-timing project and save it. The counterpart of the legacy
-- reaper-lsdj-{midi,arduinoboy}-author.lua trio, parameterized by RP_SCENARIO:
--
--   midi-metro       empty RetroPlug MIDI item; host transport clocks MidiSync (mode 1). 8 s.
--   arduinoboy-metro RetroPlug MIDI item with note 24 @ t=0 (arm play) + note 25 near the end
--                    (disable); MidiSyncArduinoboy (mode 2). 8 s.
--   midi-drift       like midi-metro but long (REAPER_AUTHOR_DURATION, default 180 s) for per-beat drift.
--
-- Same two-track shape as legacy: track 1 = the plugin ("RetroPlug", pan hard-L),
-- track 2 = ReaSynth click (pan hard-R, one short C-5 per quarter beat) → the stereo analyzer separates
-- LSDj (L) from the metronome grid (R). Letting Reaper add the FX by name captures the correct scanned
-- VST3 GUID. The LSDj project itself loads from the autoloaded .rplg (RETROPLUG_AUTOLOAD_PROJECT), so the
-- .rpp stays a template; the render re-applies the fixture fresh.

local dest       = os.getenv("REAPER_AUTHOR_DEST") or "/tmp/lsdj.rpp"
local render_dir = os.getenv("REAPER_AUTHOR_RENDER_DIR") or ""
local scenario   = os.getenv("RP_SCENARIO") or "midi-metro"

local logf = io.open("/tmp/reaper-author-script.log", "w")
local function log(msg)
    if logf then logf:write(msg .. "\n"); logf:flush() end
    reaper.ShowConsoleMsg(msg .. "\n")
end
log("[author] start, dest=" .. dest .. " scenario=" .. scenario .. " render_dir=" .. render_dir)

local BPM = 120
local DURATION = 8.0
local RENDER_STEM
if scenario == "midi-metro" then
    RENDER_STEM = "reaper-lsdj-midi-metro"
elseif scenario == "arduinoboy-metro" then
    RENDER_STEM = "reaper-lsdj-arduinoboy-metro"
elseif scenario == "midi-drift" then
    RENDER_STEM = "reaper-lsdj-midi-drift"
    DURATION = tonumber(os.getenv("REAPER_AUTHOR_DURATION")) or 180.0
else
    log("ERROR: unknown RP_SCENARIO " .. scenario)
    return
end

reaper.Main_OnCommand(40023, 0)  -- File: New project
reaper.SetCurrentBPM(0, BPM, false)

-- Track 1: the RetroPlug, panned hard-left.
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
if scenario == "arduinoboy-metro" then
    -- Arduinoboy play-enable (note 24) at t=0, disable (note 25) near the end.
    local tk_rp = reaper.GetMediaItemTake(it_rp, 0)
    local function ppq_rp(t) return reaper.MIDI_GetPPQPosFromProjTime(tk_rp, t) end
    reaper.MIDI_InsertNote(tk_rp, false, false, ppq_rp(0.0), ppq_rp(0.05), 0, 24, 100, false)
    reaper.MIDI_InsertNote(tk_rp, false, false, ppq_rp(DURATION - 0.2), ppq_rp(DURATION - 0.15), 0, 25, 100, false)
    reaper.MIDI_Sort(tk_rp)
    log("[author] arduinoboy note 24/25 inserted")
end

-- Track 2: Click, ReaSynth, panned hard-right — one short C-5 per quarter beat.
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
local BEAT_SEC = 60.0 / BPM
local n_beats = math.floor(DURATION / BEAT_SEC + 0.5)
for i = 0, n_beats - 1 do
    local t_on = i * BEAT_SEC
    reaper.MIDI_InsertNote(tk_ck, false, false, ppq_ck(t_on), ppq_ck(t_on + 0.05), 0, 72, 100, false)
end
reaper.MIDI_Sort(tk_ck)
log("[author] inserted " .. n_beats .. " click notes over " .. DURATION .. "s")

if render_dir ~= "" then
    reaper.GetSetProjectInfo_String(0, "RENDER_FILE", render_dir, true)
    reaper.GetSetProjectInfo_String(0, "RENDER_PATTERN", RENDER_STEM, true)
end
reaper.GetSetProjectInfo(0, "RENDER_SETTINGS", 0, true)
reaper.GetSetProjectInfo(0, "RENDER_BOUNDSFLAG", 1, true)
reaper.GetSetProjectInfo(0, "RENDER_CHANNELS", 2, true)
reaper.GetSetProjectInfo(0, "RENDER_SRATE", 44100, true)
reaper.GetSetProjectInfo_String(0, "RENDER_FORMAT", "ZXZhdxgAAAA=", true)

reaper.Main_SaveProjectEx(0, dest, 0)
log("[author] saved " .. dest)

reaper.Main_OnCommand(40004, 0)  -- File: Quit REAPER
