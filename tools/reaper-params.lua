-- Drives the per-ROM parameter-name check (tools/run-reaper-params.sh).
--
-- Sequence: insert RetroPlug, float the editor (start menu, no project), dump every parameter name the
-- host can see, then wait for the shell to click-load mGB through the UI. Once loaded, settle for a few
-- seconds (the editor's idle poll re-reads the map at a low cadence, and the host then has to act on the
-- restart flag) and dump the names again. The shell compares the two dumps.
--
-- Reading the names through ReaScript is the point: it is the HOST's view of the parameter info, so a
-- host that ignored the restart/rescan would show the first dump's names in the second.
local SIG_DIR = os.getenv("RP_PARAMS_SIGDIR") or "/tmp"
local OUT     = SIG_DIR .. "/rp-params.txt"
local LOADED  = SIG_DIR .. "/rp-params-loaded"
local DONE    = SIG_DIR .. "/rp-params-done"
-- FORMAT-PREFIXED, e.g. "VST3i: RetroPlug" / "CLAPi: RetroPlug". Both formats can be staged at once and
-- Reaper picks whichever it finds first for a bare "RetroPlug", which silently re-ran the VST3 leg
-- twice - so the name is explicit, there is no fallback, and the format actually loaded is recorded in
-- the dump for the shell to check.
local FXNAME  = os.getenv("RP_PARAMS_FX") or "VST3i: RetroPlug"
local SETTLE  = tonumber(os.getenv("RP_PARAMS_SETTLE") or "8")

local function L(s) local f = io.open(SIG_DIR .. "/rp-params-lua.log", "a"); if f then f:write(s .. "\n"); f:close() end end
local function exists(p) local f = io.open(p, "r"); if f then f:close(); return true end; return false end

local function dump(tag, tr, fx)
  local n = reaper.TrackFX_GetNumParams(tr, fx)
  local f = io.open(OUT, "a")
  if not f then L("cannot open " .. OUT); return end
  f:write(string.format("#%s\tcount=%d\n", tag, n))
  for i = 0, n - 1 do
    local _, nm = reaper.TrackFX_GetParamName(tr, fx, i, "")
    f:write(string.format("%s\t%d\t%s\n", tag, i, nm or ""))
  end
  f:close()
  L(tag .. " dumped " .. n .. " params")
end

reaper.InsertTrackAtIndex(0, false)
local tr = reaper.GetTrack(0, 0)
local fx, tries, phase, t = -1, 0, "adding", reaper.time_precise()

local function loop()
  local now = reaper.time_precise()
  if phase == "adding" then
    tries = tries + 1
    fx = reaper.TrackFX_AddByName(tr, FXNAME, false, -1)
    if fx >= 0 then
      local _, nm = reaper.TrackFX_GetFXName(tr, fx, "")
      L("added: " .. tostring(nm))
      local f = io.open(OUT, "a")
      if f then f:write(string.format("#fx\t%s\n", nm or "")); f:close() end
      reaper.TrackFX_Show(tr, fx, 3) -- float the editor: the UI is how the ROM gets loaded
      t = now; phase = "shown"
    elseif tries >= 120 then
      L("giveup-add"); os.execute("touch " .. DONE); phase = "done"
    end
  elseif phase == "shown" and now - t > 3.0 then
    dump("before", tr, fx)   -- no project loaded: every CC slot is generic and hidden
    os.execute("touch " .. SIG_DIR .. "/rp-params-before")
    phase = "waiting"
  elseif phase == "waiting" and exists(LOADED) then
    t = now; phase = "settling"
  elseif phase == "settling" and now - t > SETTLE then
    dump("after", tr, fx)    -- mGB loaded: its CC map should have re-labelled the slots
    L("done"); os.execute("touch " .. DONE); phase = "done"
  end
  reaper.defer(loop)
end
loop()
