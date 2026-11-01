<#
Run the Reaper render scenarios on Windows - the counterpart of tools/run-reaper-suite.sh.

    tools\run-reaper-suite.ps1                  # every scenario
    tools\run-reaper-suite.ps1 lsdj-midi-drift  # just these
    tools\run-reaper-suite.ps1 -List            # names only

The Linux suite fans its jobs out concurrently, each isolated by a per-job X display and JACK
server. This one runs them SEQUENTIALLY and that is not a shortcut: on Windows there is a single
desktop, and Reaper's audio device and plugin windows are process-global, so two jobs would
contend for them. Wall-clock is therefore the sum, not the max.

Each scenario is three steps, the same three the pnpm reaper:* scripts run on Linux:
  1. a node authoring tool writes the .rplg fixture (the plugin's autoload state)
  2. run-reaper-author.ps1 runs a ReaScript that builds and saves the .rpp
  3. run-reaper-render.ps1 renders it, then reaper-timing-analyze.py judges the WAV

NOTE ON DRIFT: the scenario table below mirrors the reaper:* entries in package.json. Those
remain the definition of record for Linux; if one changes there, change it here too. The two
are kept apart rather than unified because the Linux entries are shell one-liners that cannot
run on Windows, and rewriting them cross-platform would churn the working Linux path.
#>

[CmdletBinding()]
param(
    [Parameter(ValueFromRemainingArguments)] [string[]] $Only,
    [switch] $List
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Continue'

$repo = Split-Path $PSScriptRoot -Parent
$tools = $PSScriptRoot

# Resolve a real Python. The Microsoft Store stub in WindowsApps is on PATH by default and is
# NOT a Python - it only offers to install one - so prefer a genuine interpreter and say so
# clearly when there is none, rather than failing later inside the analyzer.
function Get-Python {
    $candidates = @()
    $candidates += Get-ChildItem "$env:LOCALAPPDATA\Programs\Python\Python*\python.exe" -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending | Select-Object -ExpandProperty FullName
    $candidates += Get-ChildItem "$env:ProgramFiles\Python*\python.exe" -ErrorAction SilentlyContinue |
        Select-Object -ExpandProperty FullName
    foreach ($c in $candidates) { if (Test-Path $c) { return $c } }
    throw 'run-reaper-suite: no real Python found. The WindowsApps python.exe is a Store stub, not an interpreter. Install Python (winget install Python.Python.3.13) and `pip install numpy`.'
}

# tag, fixture (node argv), scenario env, rpp, author lua, render stem, analyzer args, block size
$scenarios = @(
    @{ Tag = 'mgb-smoke'
       Fixture = @('tools/author-rplg.js')
       Rpp = 'examples/reaper/mgb_smoke.rpp'; Lua = 'tools/reaper-mgb-author.lua'
       Rplg = 'build/mgb.rplg.zip'; Stem = 'reaper-mgb-smoke'; Analyze = $null }

    @{ Tag = 'mgb-midi-timing'
       Fixture = @('tools/author-rplg.js')
       Rpp = 'examples/reaper/mgb_midi_timing.rpp'; Lua = 'tools/reaper-mgb-timing-author.lua'
       Rplg = 'build/mgb.rplg.zip'; Stem = 'reaper-mgb-midi-timing'; Period = '8192'
       Analyze = @('--midi-timing', '--from-ms', '2000', '--gap-ms', '136.05', '--tol-ms', '25') }

    @{ Tag = 'n8-midi-timing'
       Fixture = @('tools/author-nes-rplg.js')
       Rpp = 'examples/reaper/nes_midi_timing.rpp'; Lua = 'tools/reaper-nes-timing-author.lua'
       Rplg = 'build/nes.rplg.zip'; Stem = 'reaper-nes-midi-timing'; Period = '8192'
       Analyze = @('--midi-timing', '--from-ms', '2000', '--gap-ms', '136.05', '--tol-ms', '30') }

    @{ Tag = 'lsdj-midi-metro'
       Fixture = @('tools/author-lsdj-rplg.js', 'midi-metro'); Scenario = 'midi-metro'
       Rpp = 'examples/reaper/lsdj_midi_metro.rpp'; Lua = 'tools/reaper-lsdj-author.lua'
       Rplg = 'build/lsdj_midi-metro.rplg.zip'; Stem = 'reaper-lsdj-midi-metro'; Analyze = @() }

    @{ Tag = 'lsdj-arduinoboy-metro'
       Fixture = @('tools/author-lsdj-rplg.js', 'arduinoboy-metro'); Scenario = 'arduinoboy-metro'
       Rpp = 'examples/reaper/lsdj_arduinoboy_metro.rpp'; Lua = 'tools/reaper-lsdj-author.lua'
       Rplg = 'build/lsdj_arduinoboy-metro.rplg.zip'; Stem = 'reaper-lsdj-arduinoboy-metro'; Analyze = @() }

    @{ Tag = 'lsdj-midi-drift'
       Fixture = @('tools/author-lsdj-rplg.js', 'midi-drift'); Scenario = 'midi-drift'
       Rpp = 'examples/reaper/lsdj_midi_drift.rpp'; Lua = 'tools/reaper-lsdj-author.lua'
       Rplg = 'build/lsdj_midi-drift.rplg.zip'; Stem = 'reaper-lsdj-midi-drift'; Analyze = @('--drift') }

    @{ Tag = 'sms-sync'
       Fixture = @('tools/author-sms-rplg.js')
       Rpp = 'examples/reaper/sms_sync.rpp'; Lua = 'tools/reaper-sms-author.lua'
       Rplg = 'build/sms.rplg.zip'; Stem = 'reaper-sms-sync'; Analyze = @('--drift') }

    @{ Tag = 'gg-sync'
       Fixture = @('tools/author-sms-rplg.js', 'resources/roms/smsggdj_v0_45.gg')
       Rpp = 'examples/reaper/gg_sync.rpp'; Lua = 'tools/reaper-sms-author.lua'
       Rplg = 'build/gg.rplg.zip'; Stem = 'reaper-gg-sync'; Analyze = @('--drift')
       AuthorStem = 'reaper-gg-sync' }

    @{ Tag = 'risa-sync'
       Fixture = @('tools/author-risa-rplg.js')
       Rpp = 'examples/reaper/risa_sync.rpp'; Lua = 'tools/reaper-risa-author.lua'
       Rplg = 'build/risa.rplg.zip'; Stem = 'reaper-risa-sync'; Analyze = @('--drift') }
)

if ($List) { $scenarios | ForEach-Object { $_.Tag }; exit 0 }

$selected = if ($Only) {
    $bad = $Only | Where-Object { $t = $_; -not ($scenarios | Where-Object { $_.Tag -eq $t }) }
    if ($bad) { throw "unknown scenario(s): $($bad -join ', ')  (try -List)" }
    $scenarios | Where-Object { $Only -contains $_.Tag }
} else { $scenarios }

$python = Get-Python
Set-Location $repo
Write-Host "reaper suite (windows): $($selected.Count) scenario(s), sequential"
Write-Host "  python: $python"

$results = @()
foreach ($s in $selected) {
    Write-Host ""
    Write-Host "==> $($s.Tag)" -ForegroundColor Cyan
    $status = 'PASS'
    $note = ''

    # Per-scenario env. Set explicitly every iteration (including the clears) so one scenario's
    # RP_SCENARIO / block size cannot leak into the next.
    $env:RP_JOB_TAG = $s.Tag
    $env:RP_SCENARIO = if ($s.ContainsKey('Scenario')) { $s.Scenario } else { $null }
    $env:REAPER_JACK_PERIOD = if ($s.ContainsKey('Period')) { $s.Period } else { $null }
    $env:REAPER_AUTHOR_STEM = if ($s.ContainsKey('AuthorStem')) { $s.AuthorStem } else { $null }

    try {
        & node @($s.Fixture) 2>&1 | Select-Object -Last 1 | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "fixture step failed ($LASTEXITCODE)" }

        # Several scenarios need a ROM that is not in the repo (risa, and any LSDj build the
        # machine happens not to have). Those authoring tools print "SKIP: ... not found", write
        # no fixture, and exit 0 - so the absence of the .rplg is the signal, not the exit code.
        # Report that as SKIP: a missing third-party asset is not a failing test.
        if (-not (Test-Path (Join-Path $repo $s.Rplg))) {
            $status = 'SKIP'
            $note = "no fixture ($($s.Rplg)) - asset missing"
            Write-Host "  $note" -ForegroundColor Yellow
            $results += [pscustomobject]@{ Scenario = $s.Tag; Status = $status; Note = $note }
            continue
        }

        $env:RP_JOB_TAG = "author-$($s.Tag)"
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $tools 'run-reaper-author.ps1') `
            $s.Rpp 'build' $s.Lua $s.Rplg 2>&1 | Select-Object -Last 1 | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "author step failed ($LASTEXITCODE)" }

        $wav = Join-Path $repo "build\$($s.Stem).wav"
        if (Test-Path $wav) { Remove-Item $wav -Force }

        $env:RP_JOB_TAG = $s.Tag
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $tools 'run-reaper-render.ps1') `
            $s.Rpp $s.Rplg 2>&1 | Select-Object -Last 1 | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "render step failed ($LASTEXITCODE)" }
        if (-not (Test-Path $wav)) { throw "render produced no $($s.Stem).wav" }

        if ($null -ne $s.Analyze) {
            $args_ = @((Join-Path $tools 'reaper-timing-analyze.py')) + $s.Analyze + @($wav)
            $out = & $python @args_ 2>&1
            $out | Select-Object -Last 6 | Out-Host
            if ($LASTEXITCODE -ne 0) { throw "analyzer FAILED" }
        } else {
            $len = (Get-Item $wav).Length
            Write-Host "  rendered $($s.Stem).wav ($len bytes; no analyzer for this scenario)"
        }
    }
    catch {
        $status = 'FAIL'
        $note = $_.Exception.Message
        Write-Host "  $note" -ForegroundColor Red
    }
    $results += [pscustomobject]@{ Scenario = $s.Tag; Status = $status; Note = $note }
}

$env:RP_SCENARIO = $null; $env:REAPER_JACK_PERIOD = $null; $env:REAPER_AUTHOR_STEM = $null; $env:RP_JOB_TAG = $null

Write-Host ""
$results | Format-Table -AutoSize | Out-Host
$failed = @($results | Where-Object { $_.Status -eq 'FAIL' })
$skipped = @($results | Where-Object { $_.Status -eq 'SKIP' })
if ($failed.Count) { Write-Host "FAILED: $($failed.Scenario -join ', ')" -ForegroundColor Red; exit 1 }
$msg = "all reaper scenarios passed"
if ($skipped.Count) { $msg += " ($($skipped.Count) skipped: $($skipped.Scenario -join ', '))" }
Write-Host $msg -ForegroundColor Green
exit 0
