--[[----------------------------------------------------------------------------
RetroPlug Renoise harness.

Renoise has no render CLI, so this tool IS the CLI: it runs automatically when
Renoise starts, reads a job spec, builds a song around the RetroPlug plugin,
renders it offline and quits. tools/run-renoise-render.ps1 writes the spec,
launches Renoise and waits for the result file.

Why a second host at all, when Reaper already renders the plugin? Because every
host-integration bug this repo has hit - the class-id counter collision that made
the DAW-hosted editor render blank, the per-context window-hook routing - is a bug
about how ONE host drives the plugin. A single host cannot catch those; a second
one with different threading, parameter and MIDI-delivery behaviour can.

The spec is JSON-ish but parsed by hand: Renoise's Lua has no JSON library and a
tool may not require() outside its own bundle, so the format is deliberately flat
(one key=value per line) rather than pulling in a parser.

TWO MODES, and the reason there are two:

  mode=render   builds the song and renders it to WAV. This needs a REGISTERED Renoise -
                song:render() is disabled in the demo, and Renoise reports that by popping a
                modal dialog rather than returning an error to Lua, so the tool never regains
                control and the runner sees a hang. The runner looks for that specific line in
                Renoise's Log.txt and says so.
  mode=probe    instantiates the plugin and dumps what the host sees of it - name, parameter
                count, parameter names - then quits. No rendering, so it works in the demo.
                This is not a poor substitute: instantiation and the parameter surface are
                exactly where the host-specific bugs in this repo have lived (the class-id
                collision, the per-ROM parameter relabelling). It just cannot judge audio.

Job spec keys (written by the runner into RETROPLUG_RENOISE_JOB):
  mode=render|probe (default render)
  out=<abs path to the .wav to write>            (render mode)
  result=<abs path to the result file this tool writes>
  plugin=<substring matched against available_plugins, default "RetroPlug">
  bpm=<number, default 120>
  lines=<pattern length in lines, default 64>
  note=<note number to trigger, default 48 (C-4)>
  note_line=<line to place the note on, default 0>
  samplerate=<default 44100>
  bitdepth=<default 24>

The result file is how the runner learns what happened, since a GUI app's stdout
is not readable: it gets "ok" plus diagnostics, or "error <message>". The runner
treats a missing result file as a hang.
------------------------------------------------------------------------------]]

local function log(msg)
  print("[retroplug-harness] " .. tostring(msg))
end

-- Read the whole of a file, or nil. io is available to tools; os.getenv is not
-- restricted either, but the runner passes the spec PATH via an env var so the
-- spec itself can be arbitrarily long without quoting problems.
local function read_file(path)
  local f = io.open(path, "r")
  if not f then return nil end
  local s = f:read("*a")
  f:close()
  return s
end

local function write_file(path, text)
  local f = io.open(path, "w")
  if not f then return false end
  f:write(text)
  f:close()
  return true
end

local function parse_spec(text)
  local spec = {}
  for line in text:gmatch("[^\r\n]+") do
    if not line:match("^%s*#") then
      local k, v = line:match("^%s*([%w_]+)%s*=%s*(.-)%s*$")
      if k then spec[k] = v end
    end
  end
  return spec
end

local function num(spec, key, default)
  local v = tonumber(spec[key] or "")
  if v == nil then return default end
  return v
end

-- Find the plugin whose identifier contains `needle`. available_plugins entries look
-- like "Audio/Generators/VST3/RetroPlug"; matching a substring keeps this working
-- whether the VST2, VST3 or AU build is the one Renoise found.
local function find_plugin(needle)
  local plugs = renoise.song().instruments[1].plugin_properties.available_plugins
  local lowered = needle:lower()
  for _, p in ipairs(plugs) do
    if p:lower():find(lowered, 1, true) then return p end
  end
  return nil, plugs
end

-- What the host makes of the plugin, as text. This is the probe-mode payload: if Renoise
-- instantiates RetroPlug at all, and reports a sane parameter surface, the integration that
-- matters is working even though the demo will not let us render it.
local function describe_plugin(instr, result_path)
  local pp = instr.plugin_properties
  local dev = pp.plugin_device
  local out = { "ok", "mode=probe" }
  out[#out + 1] = "device_path=" .. tostring(pp.plugin_device and dev.device_path or "?")
  out[#out + 1] = "name=" .. tostring(dev and dev.name or "?")
  out[#out + 1] = "external_editor_available=" .. tostring(dev and dev.external_editor_available)
  local params = dev and dev.parameters or {}
  out[#out + 1] = "parameter_count=" .. tostring(#params)
  for i = 1, math.min(#params, 24) do
    out[#out + 1] = string.format("param[%d]=%s", i, tostring(params[i].name))
  end
  write_file(result_path, table.concat(out, "\n") .. "\n")
  log("probe wrote " .. #params .. " parameters")
end

local function run_job(spec_path)
  local text = read_file(spec_path)
  if not text then return nil, "job spec unreadable: " .. spec_path end
  local spec = parse_spec(text)

  local mode = spec.mode or "render"
  local out = spec.out
  if mode == "render" and not out then return nil, "job spec has no out=" end

  local song = renoise.song()
  song.transport.bpm = num(spec, "bpm", 120)

  -- Instrument 1 hosts the plugin.
  local instr = song.instruments[1]
  local needle = spec.plugin or "RetroPlug"
  local path, all = find_plugin(needle)
  if not path then
    local n = all and #all or 0
    return nil, string.format("plugin matching %q not found among %d available plugins", needle, n)
  end
  log("loading " .. path)
  instr.plugin_properties:load_plugin(path)
  if not instr.plugin_properties.plugin_loaded then
    return nil, "load_plugin did not load: " .. path
  end
  instr.name = "RetroPlug"

  if mode == "probe" then
    describe_plugin(instr, spec.result or (spec_path .. ".result"))
    return true, "probe"
  end

  -- One pattern, one note. Enough to prove the plugin is instantiated, receives a
  -- note and produces audio in THIS host; the musical content is the Reaper suite's job.
  local patt = song.patterns[song.sequencer.pattern_sequence[1]]
  patt.number_of_lines = math.floor(num(spec, "lines", 64))
  local track = patt.tracks[1]
  local line = track:line(math.floor(num(spec, "note_line", 0)) + 1)
  local col = line.note_columns[1]
  col.note_value = math.floor(num(spec, "note", 48))
  col.instrument_value = 0

  local opts = {
    sample_rate = math.floor(num(spec, "samplerate", 44100)),
    bit_depth = math.floor(num(spec, "bitdepth", 24)),
    interpolation = "precise",
    priority = "high",
    start_pos = renoise.SongPos(1, 1),
    end_pos = renoise.SongPos(1, patt.number_of_lines),
  }

  log("rendering -> " .. out)
  local ok, err = song:render(opts, out, function()
    log("render callback fired")
  end)
  if not ok then return nil, "render failed: " .. tostring(err) end
  return true
end

-- Renoise renders asynchronously: song():render() returns immediately and the song
-- stays in `rendering` until it finishes. Poll rather than block, because blocking
-- the Lua thread would stall the very render we are waiting on.
local function finish(result_path, out_path)
  local song = renoise.song()
  if song.rendering then return end -- still going; the timer fires again

  local size = -1
  local f = io.open(out_path, "rb")
  if f then size = f:seek("end"); f:close() end

  write_file(result_path, string.format("ok\nwav=%s\nbytes=%d\n", out_path, size))
  log("done, wrote result (" .. size .. " bytes of wav)")
  renoise.app():quit()
end

-- Entry point. Tools load before a song exists, so the work is deferred to the
-- new-document notifier; app_new_document fires once Renoise has its initial song.
renoise.tool().app_new_document_observable:add_notifier(function()
  local spec_path = os.getenv("RETROPLUG_RENOISE_JOB")
  if not spec_path or spec_path == "" then
    return -- not a harness run; leave the user's Renoise alone
  end

  local spec = parse_spec(read_file(spec_path) or "")
  local result_path = spec.result or (spec_path .. ".result")

  local probed = false
  local ok, err = pcall(function()
    local good, msg = run_job(spec_path)
    if not good then error(msg, 0) end
    probed = (msg == "probe")
  end)

  if not ok then
    log("ERROR: " .. tostring(err))
    write_file(result_path, "error " .. tostring(err) .. "\n")
    renoise.app():quit()
    return
  end

  -- Probe mode has already written its result and has nothing to wait for.
  if probed then renoise.app():quit(); return end

  renoise.tool():add_timer(function() finish(result_path, spec.out) end, 200)
end)
