-- Drives the "loading a ROM in the SECOND instance freezes both UIs" repro
-- (tools/run-reaper-two-instances-load.sh). Reported against Renoise, reproduced by the user in
-- Reaper: instance 1 with a ROM is fine, a second editor showing the start menu is fine, and loading
-- a ROM into that second instance freezes BOTH editors.
--
-- The shell owns the mouse (keyboard doesn't reach a plugin editor headlessly), so the two sides
-- hand off through signal files in RP_TWO_SIGDIR:
--   e1-shown   (we write)  instance 1 inserted on track 1, editor floated
--   go2        (shell writes, after it has click-loaded a ROM into instance 1)
--   e2-shown   (we write)  instance 2 inserted on track 2, editor floated -- the shell clicks next
--
-- The HEARTBEAT is the point of this script. Reaper runs defer callbacks on the main thread, which is
-- also the thread both plugin editors idle on, so a UI thread that blocks stops the counter dead.
-- That's what makes a freeze mechanically detectable rather than something you have to eyeball.
local SIG_DIR = os.getenv("RP_TWO_SIGDIR") or "/tmp"
local HB      = SIG_DIR .. "/hb"

local function L(s) local f = io.open(SIG_DIR .. "/two-load-lua.log", "a"); if f then f:write(s .. "\n"); f:close() end end
local function touch(p) local f = io.open(p, "w"); if f then f:write("1"); f:close() end end
local function exists(p) local f = io.open(p, "r"); if f then f:close(); return true end; return false end

local FXNAME = os.getenv("RP_TWO_FX") or "VST3i: RetroPlug"

local tracks, fx = {}, {}
local function addInstance(n)
  reaper.InsertTrackAtIndex(n - 1, false)
  local tr = reaper.GetTrack(0, n - 1)
  if not tr then return false end
  local idx = reaper.TrackFX_AddByName(tr, FXNAME, false, -1)
  if idx < 0 then reaper.DeleteTrack(tr); return false end
  tracks[n], fx[n] = tr, idx
  reaper.TrackFX_Show(tr, idx, 3) -- 3 = float the FX window (creates the editor)
  L("instance " .. n .. " floated")
  return true
end

local phase, tries = "add1", 0
local beats, lastBeat = 0, 0

local function loop()
  local now = reaper.time_precise()

  if phase == "add1" then
    tries = tries + 1
    if addInstance(1) then
      touch(SIG_DIR .. "/e1-shown"); phase = "wait-go2"; tries = 0
    elseif tries >= 300 then
      L("giveup: '" .. FXNAME .. "' not found"); touch(SIG_DIR .. "/giveup"); phase = "beating"
    end
  elseif phase == "wait-go2" then
    -- The shell click-loads a ROM into instance 1 first; only then does the second instance arrive,
    -- which is the ordering the report describes.
    if exists(SIG_DIR .. "/go2") then phase = "add2"; tries = 0 end
  elseif phase == "add2" then
    tries = tries + 1
    if addInstance(2) then
      touch(SIG_DIR .. "/e2-shown"); phase = "beating"
    elseif tries >= 300 then
      L("giveup: second instance"); touch(SIG_DIR .. "/giveup"); phase = "beating"
    end
  end

  -- Heartbeat, throttled so it isn't a write per frame. A stalled counter == a blocked main thread.
  beats = beats + 1
  if now - lastBeat > 0.25 then
    lastBeat = now
    local f = io.open(HB, "w")
    if f then f:write(tostring(beats)); f:close() end
  end

  reaper.defer(loop)
end

loop()
