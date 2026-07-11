-- Insert RetroPlug on a fresh track and float its editor, then hold Reaper open (defer loop) so the
-- editor renders and the plugin's RETROPLUG_SCREENSHOT_PATH hook can dump the LVGL screen. Retries the
-- AddByName until the VST scan has registered the plugin. Diagnostics go to /tmp/reaper-editor-lua.log.
-- Driven by tools/run-reaper-editor.sh.
local log = io.open("/tmp/reaper-editor-lua.log", "w")
local function L(s) if log then log:write(s .. "\n"); log:flush() end end

reaper.InsertTrackAtIndex(0, false)
local tr = reaper.GetTrack(0, 0)
L("track present: " .. tostring(tr ~= nil))

local function hold()
  reaper.defer(hold)
end

local tries = 0
local function tryAdd()
  tries = tries + 1
  local fx = reaper.TrackFX_AddByName(tr, "RetroPlug", false, -1)
  if fx < 0 then fx = reaper.TrackFX_AddByName(tr, "retroplug", false, -1) end
  if fx >= 0 then
    local _, nm = reaper.TrackFX_GetFXName(tr, fx, "")
    L("added fx after " .. tries .. " tries: " .. tostring(nm) .. " (idx " .. fx .. ")")
    reaper.TrackFX_Show(tr, fx, 3) -- 3 = float the FX window (creates the editor)
    L("TrackFX_Show(float) issued")
    hold()
  elseif tries < 60 then
    reaper.defer(tryAdd)
  else
    L("GAVE UP: plugin not found after " .. tries .. " tries (scan failed?)")
    hold()
  end
end
tryAdd()
