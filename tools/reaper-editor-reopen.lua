-- Drives the close→reopen state-loss repro (tools/run-reaper-editor-reopen.sh).
--
-- Sequence: insert RetroPlug, float the editor (start menu), then wait for the shell to click-load
-- mGB (it touches /tmp/rp-editor-reopen-loaded once the grid is up). Then CLOSE the editor window
-- and REOPEN it, and snapshot the reopened editor. The close/reopen timing is driven here because
-- only the script knows exactly when it closed; the load-click and the menu/loaded snapshots are the
-- shell's. Snapshots are copies of the plugin's RETROPLUG_SCREENSHOT_PATH dump taken at each phase.
local SNAP    = os.getenv("RETROPLUG_SCREENSHOT_PATH")
local SIG_DIR = os.getenv("RP_EDITOR_REOPEN_SIGDIR") or "/tmp"
local LOADED  = SIG_DIR .. "/rp-editor-reopen-loaded"
local DONE    = SIG_DIR .. "/rp-editor-reopen-done"

local function L(s) local f = io.open(SIG_DIR .. "/rp-editor-reopen-lua.log", "a"); if f then f:write(s .. "\n"); f:close() end end
local function cp(dst) if SNAP then os.execute(string.format('cp -f %q %q 2>/dev/null', SNAP, dst)) end end
local function exists(p) local f = io.open(p, "r"); if f then f:close(); return true end; return false end

reaper.InsertTrackAtIndex(0, false)
local tr = reaper.GetTrack(0, 0)
local fx, tries, phase, t = -1, 0, "adding", reaper.time_precise()

local function loop()
  local now = reaper.time_precise()
  if phase == "adding" then
    tries = tries + 1
    fx = reaper.TrackFX_AddByName(tr, "RetroPlug", false, -1)
    if fx < 0 then fx = reaper.TrackFX_AddByName(tr, "retroplug", false, -1) end
    if fx >= 0 then
      reaper.TrackFX_Show(tr, fx, 3) -- float the editor
      L("floated"); phase = "shown"
    elseif tries >= 120 then
      L("giveup-add"); os.execute("touch " .. DONE); phase = "done"
    end
  elseif phase == "shown" and exists(LOADED) then
    reaper.TrackFX_Show(tr, fx, 2) -- close the floating editor window (destroys the view)
    L("closed"); t = now; phase = "closed"
  elseif phase == "closed" and now - t > 2.0 then
    reaper.TrackFX_Show(tr, fx, 3) -- reopen it (recreates the view / remounts the UI)
    L("reopened"); t = now; phase = "reopened"
  elseif phase == "reopened" and now - t > 4.0 then
    cp(SNAP .. ".reopened") -- what the reopened editor is showing
    L("done"); os.execute("touch " .. DONE); phase = "done"
  end
  reaper.defer(loop)
end
loop()
