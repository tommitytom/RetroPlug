-- reaper-mgb-author.lua
--
-- Build the mgb_smoke test project programmatically and save it.
-- Run via: reaper tools/reaper-mgb-author.lua
-- Output path comes from the REAPER_AUTHOR_DEST env var so the wrapper
-- controls where it lands.
--
-- Track layout: one track named "RetroPlug" with a RetroPlug VST3i FX +
-- a 4-second MIDI item containing a C-major chord (C4/E4/G4) from 1.5s
-- to 3.0s. Mirrors examples/scripts/mgb_smoke.json so the same content is
-- exercised through both the CLI and the plugin host.

local dest = os.getenv("REAPER_AUTHOR_DEST") or "/tmp/mgb_smoke.rpp"
local render_dir = os.getenv("REAPER_AUTHOR_RENDER_DIR") or ""

-- Tee status to a log file so the headless wrapper can read it
-- without scraping the ReaScript console window.
local logf = io.open("/tmp/reaper-author-script.log", "w")
local function log(msg)
    if logf then logf:write(msg .. "\n"); logf:flush() end
    reaper.ShowConsoleMsg(msg .. "\n")
end
log("[author] start, dest=" .. dest .. " render_dir=" .. render_dir)

-- New project
reaper.Main_OnCommand(40023, 0)  -- File: New project

-- Insert one track + name it
reaper.InsertTrackAtIndex(0, true)
local track = reaper.GetTrack(0, 0)
reaper.GetSetMediaTrackInfo_String(track, "P_NAME", "RetroPlug", true)

-- Add RetroPlug VST3i. TrackFX_AddByName respects the standard prefix:
-- "VST3i:" forces VST3 instrument matching, avoiding any name collision
-- with the standalone or unrelated plugins.
local fxidx = reaper.TrackFX_AddByName(track, "VST3i:RetroPlug", false, -1)
if fxidx < 0 then
    log("ERROR: RetroPlug VST3i not found on plugin path (VST3_PATH=" ..
        (os.getenv("VST3_PATH") or "") .. ")")
    return
end
log("[author] FX added at index " .. fxidx)

-- 4-second MIDI item, C-major chord at 1.5s -> 3.0s, velocity 100.
local item = reaper.CreateNewMIDIItemInProj(track, 0, 4, false)
local take = reaper.GetMediaItemTake(item, 0)
local function ppq(t) return reaper.MIDI_GetPPQPosFromProjTime(take, t) end
for _, note in ipairs({60, 64, 67}) do
    reaper.MIDI_InsertNote(take, false, false, ppq(1.5), ppq(3.0),
                           0, note, 100, false)
end
reaper.MIDI_Sort(take)

-- Render settings: WAV, 16-bit, render entire project bounds. RENDER_FILE
-- is resolved relative to the project's directory, so the caller must
-- pass an absolute path here for the output to land where the CMake
-- target expects.
if render_dir ~= "" then
    reaper.GetSetProjectInfo_String(0, "RENDER_FILE", render_dir, true)
    reaper.GetSetProjectInfo_String(0, "RENDER_PATTERN", "reaper-mgb-smoke", true)
end
reaper.GetSetProjectInfo(0, "RENDER_SETTINGS", 0, true)        -- master mix
reaper.GetSetProjectInfo(0, "RENDER_BOUNDSFLAG", 1, true)      -- entire project
reaper.GetSetProjectInfo(0, "RENDER_CHANNELS", 2, true)
reaper.GetSetProjectInfo(0, "RENDER_SRATE", 44100, true)
-- Format: chunk-encoded; "evaw" is the four-cc for WAV in Reaper's
-- RENDER_FORMAT field. Per Cockos docs the four bytes are reversed.
reaper.GetSetProjectInfo_String(0, "RENDER_FORMAT",
    "ZXZhdxgAAAA=", true)  -- evaw + 16-bit defaults

-- Save the project. Use SaveProjectEx so we can pass an explicit path
-- without prompting (the host has no UI thread that could display the
-- save dialog).
reaper.Main_SaveProjectEx(0, dest, 0)
log("[author] saved " .. dest)

-- Quit cleanly.
reaper.Main_OnCommand(40004, 0)  -- File: Quit REAPER
