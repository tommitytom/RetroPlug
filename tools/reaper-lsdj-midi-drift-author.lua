-- reaper-lsdj-midi-drift-author.lua
--
-- Hour-long counterpart to reaper-lsdj-midi-author.lua. Identical two-track
-- shape (RetroPlug L with an empty MIDI item; ReaSynth click R, one note per
-- beat at 120 BPM) but DURATION = 3600 s so we can profile MidiSync drift over
-- a long run. LsdjSyncMode::MidiSync emits 0xF8 clocks unconditionally on every
-- block where the host transport is playing; LSDj's START was pressed in the
-- bootstrap (lsdj_midi_drift.test.ts) to arm "WAIT for clock", captured in the
-- autoload .rplg. The .rplg's song clicks a short noise hit on every beat so the
-- analyzer can pair each LSDj onset to its reference click.

local dest = os.getenv("REAPER_AUTHOR_DEST") or "/tmp/lsdj_midi_drift.rpp"
local render_dir = os.getenv("REAPER_AUTHOR_RENDER_DIR") or ""

local logf = io.open("/tmp/reaper-author-script.log", "w")
local function log(msg)
    if logf then logf:write(msg .. "\n"); logf:flush() end
    reaper.ShowConsoleMsg(msg .. "\n")
end
log("[author] start, dest=" .. dest .. " render_dir=" .. render_dir)

-- Default to a 1 h render; REAPER_AUTHOR_DURATION overrides it (seconds) so the
-- whole render+analyze chain can be smoke-tested in a few seconds.
local DURATION = tonumber(os.getenv("REAPER_AUTHOR_DURATION")) or 3600.0
local BPM      = 120

reaper.Main_OnCommand(40023, 0)
reaper.SetCurrentBPM(0, BPM, false)

-- Track 1: RetroPlug, panned hard-left.
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

-- Empty MIDI item just to keep the track's MIDI lane represented in the
-- project. Reaper's transport drives clocks regardless of items.
local it_rp = reaper.CreateNewMIDIItemInProj(t_rp, 0, DURATION, false)

-- Track 2: Click, ReaSynth, panned hard-right.
reaper.InsertTrackAtIndex(1, true)
local t_ck = reaper.GetTrack(0, 1)
reaper.GetSetMediaTrackInfo_String(t_ck, "P_NAME", "Click", true)
reaper.SetMediaTrackInfo_Value(t_ck, "D_PAN", 1.0)

local fx_ck = reaper.TrackFX_AddByName(t_ck, "ReaSynth", false, -1)
if fx_ck < 0 then
    log("ERROR: ReaSynth not found")
    return
end
log("[author] ReaSynth FX at index " .. fx_ck)

local it_ck = reaper.CreateNewMIDIItemInProj(t_ck, 0, DURATION, false)
local tk_ck = reaper.GetMediaItemTake(it_ck, 0)
local function ppq_ck(t) return reaper.MIDI_GetPPQPosFromProjTime(tk_ck, t) end
local BEAT_SEC = 60.0 / BPM
local n_beats = math.floor(DURATION / BEAT_SEC + 0.5)
for i = 0, n_beats - 1 do
    local t_on = i * BEAT_SEC
    reaper.MIDI_InsertNote(tk_ck, false, false,
                           ppq_ck(t_on), ppq_ck(t_on + 0.05),
                           0, 72, 100, false)
end
reaper.MIDI_Sort(tk_ck)
log("[author] inserted " .. n_beats .. " click notes")

if render_dir ~= "" then
    reaper.GetSetProjectInfo_String(0, "RENDER_FILE", render_dir, true)
    reaper.GetSetProjectInfo_String(0, "RENDER_PATTERN",
        "reaper-lsdj-midi-drift", true)
end
reaper.GetSetProjectInfo(0, "RENDER_SETTINGS", 0, true)
reaper.GetSetProjectInfo(0, "RENDER_BOUNDSFLAG", 1, true)
reaper.GetSetProjectInfo(0, "RENDER_CHANNELS", 2, true)
reaper.GetSetProjectInfo(0, "RENDER_SRATE", 44100, true)
reaper.GetSetProjectInfo_String(0, "RENDER_FORMAT",
    "ZXZhdxgAAAA=", true)

reaper.Main_SaveProjectEx(0, dest, 0)
log("[author] saved " .. dest)

reaper.Main_OnCommand(40004, 0)
