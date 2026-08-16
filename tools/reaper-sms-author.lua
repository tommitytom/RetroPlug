-- reaper-sms-author.lua
--
-- Serves both machines: REAPER_AUTHOR_STEM picks the render stem (and so which project this is), the
-- autoloaded .rplg carries the ROM. The Game Gear build clocks the same counter on its EXT parallel
-- port rather than controller port 2, which changes nothing on the DAW side.
--
-- Author the smsggdj host-sync DAW-timing project and save it. The Master System counterpart of
-- reaper-risa-author.lua, and structurally identical to it: SMS host sync is driven by the TRANSPORT
-- alone, so the plugin track carries NO MIDI item. RetroPlug's rom provider attaches `sms-sync` off the
-- ROM's SMSGGDJ marker when the autoloaded .rplg constructs, and the role turns the transport into a
-- 2-bit counter held on controller port 2's TR + TH lines at 24 PPQN.
--
-- Two-track shape, as the LSDj and risa renders use: track 1 = the plugin ("RetroPlug", pan hard-L),
-- track 2 = ReaSynth click (pan hard-R, one short C-5 per quarter beat), so the stereo analyzer
-- separates the cart (L) from the DAW's beat grid (R). Letting Reaper add the FX by name captures the
-- correct scanned VST3 GUID - hand-written GUIDs go stale whenever the plugin's output count changes,
-- which presents as a silent render because Reaper loads the FX offline.
--
-- The .rplg (RETROPLUG_AUTOLOAD_PROJECT) restores a core that is already ARMED: smsggdj does not
-- autoload a save, so the author script poked the metronome into the working song and tapped Play,
-- which in IN24 parks the ROM in WAIT. The first transport clock therefore starts it on the grid, with
-- no button press needed during the render. Its song hits once per beat with a percussive envelope
-- (ATK 0 / DCY 2 / HLD 0), so every beat is a transient the drift analyzer can pair to a click.

local dest       = os.getenv("REAPER_AUTHOR_DEST") or "/tmp/sms.rpp"
local render_dir = os.getenv("REAPER_AUTHOR_RENDER_DIR") or ""

local logf = io.open("/tmp/reaper-author-script.log", "w")
local function log(msg)
    if logf then logf:write(msg .. "\n"); logf:flush() end
    reaper.ShowConsoleMsg(msg .. "\n")
end
log("[author] start, dest=" .. dest .. " render_dir=" .. render_dir)

local BPM = 120
local DURATION = tonumber(os.getenv("REAPER_AUTHOR_DURATION")) or 30.0
local RENDER_STEM = os.getenv("REAPER_AUTHOR_STEM") or "reaper-sms-sync"

reaper.Main_OnCommand(40023, 0)  -- File: New project
reaper.SetCurrentBPM(0, BPM, false)

-- Track 1: the RetroPlug, panned hard-left. No MIDI item: the transport is the whole input.
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

-- An empty media item spanning the render so the track is not culled from the mix.
reaper.CreateNewMIDIItemInProj(t_rp, 0, DURATION, false)

-- Track 2: Click, ReaSynth, panned hard-right - one short C-5 per quarter beat.
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
