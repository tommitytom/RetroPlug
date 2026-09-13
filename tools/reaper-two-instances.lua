-- Load N RetroPlug instances and open ALL their editors, the way a DAW user stacking instances does
-- (reported against Renoise: a second instance crashes the host). Driven by
-- tools/run-reaper-two-instances.sh.
--
-- The point of this script is the STAGE LOG: every step appends a line to RP_STAGE_LOG and flushes,
-- so when the host dies the last line names exactly which step killed it. Without that, a crash just
-- looks like "reaper exited" and tells you nothing about whether it was the second DSP instance, the
-- second editor, or tearing one editor down while the others are live.
--
-- Stages (each waits for the previous to have settled, since a DPF editor needs idle ticks to boot):
--   fxN        one track + one RetroPlug instance, DSP only  -- N control-plane runtimes coexisting
--   editorN    float instance N's editor                     -- N LVGL displays on one thread
--   all-open   every editor has survived a few seconds of idle
--   close1     close instance 1's editor while the rest stay open  -- cross-instance LVGL teardown
--   done       everything survived
--
-- Env:
--   RP_STAGE_LOG   where to append the stage markers
--   RP_TWO_COUNT   how many instances to load (default 2)
--   RP_TWO_FX      the FX name to insert (default "VST3i: RetroPlug"); picks the plugin FORMAT, since
--                  a bare "RetroPlug" lets Reaper choose whichever format it scanned first
local stagePath = os.getenv("RP_STAGE_LOG") or "/tmp/reaper-two-instances-stages.log"
local COUNT = tonumber(os.getenv("RP_TWO_COUNT") or "2")
local FXNAME = os.getenv("RP_TWO_FX") or "VST3i: RetroPlug"

local stageLog = io.open(stagePath, "w")
local function stage(s)
  if stageLog then stageLog:write(s .. "\n"); stageLog:flush() end
end

local tracks = {}
local fx = {}

-- Insert RetroPlug on its own track. Returns false until the VST scan has registered the plugin, so
-- the caller can retry (a cold scan takes a few seconds).
local function addInstance(n)
  reaper.InsertTrackAtIndex(n - 1, false)
  local tr = reaper.GetTrack(0, n - 1)
  if not tr then return false end
  local idx = reaper.TrackFX_AddByName(tr, FXNAME, false, -1)
  if idx < 0 then
    reaper.DeleteTrack(tr) -- don't leave a bare track behind for the next attempt
    return false
  end
  tracks[n] = tr
  fx[n] = idx
  local _, nm = reaper.TrackFX_GetFXName(tr, idx, "")
  stage("fx" .. n .. " added: " .. tostring(nm))
  return true
end

-- The step list. Each entry runs once, `wait` seconds after the previous one completed — the delays
-- are what let DPF actually boot/attach an editor before the next step piles on.
local steps = {}
for n = 1, COUNT do
  steps[#steps + 1] = { wait = (n == 1) and 0.0 or 2.0, run = function() return addInstance(n) end, retry = true }
end
for n = 1, COUNT do
  steps[#steps + 1] = { wait = (n == 1) and 2.0 or 3.0, run = function()
      reaper.TrackFX_Show(tracks[n], fx[n], 3) -- 3 = float the FX window (creates the editor)
      stage("editor" .. n .. " floated")
      return true
    end }
end
steps[#steps + 1] = { wait = 5.0, run = function() stage("all-open survived"); return true end }
steps[#steps + 1] = { wait = 1.0, run = function()
    reaper.TrackFX_Show(tracks[1], fx[1], 2) -- 2 = close the floating FX window
    stage("close1 issued")
    return true
  end }
steps[#steps + 1] = { wait = 4.0, run = function() stage("done"); return true end }

local current = 1
local nextAt = reaper.time_precise()
local tries = 0

local function pump()
  if current <= #steps then
    local step = steps[current]
    local now = reaper.time_precise()
    if now >= nextAt then
      local ok = step.run()
      if ok then
        current = current + 1
        tries = 0
        nextAt = now + (steps[current] and steps[current].wait or 0)
      elseif step.retry then
        tries = tries + 1
        if tries > 300 then -- ~30s at 10 Hz: the scan never produced the plugin
          stage("GAVE UP: '" .. FXNAME .. "' not found after " .. tries .. " tries (scan failed?)")
          current = #steps + 1
        else
          nextAt = now + 0.1
        end
      else
        stage("STEP FAILED at index " .. current)
        current = #steps + 1
      end
    end
  end
  -- Keep holding after "done" so the harness sees a live host rather than a normal exit it would
  -- have to tell apart from a crash.
  reaper.defer(pump)
end

pump()
