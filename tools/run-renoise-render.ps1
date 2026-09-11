<#
Render a RetroPlug song through Renoise, headlessly.

    tools\run-renoise-render.ps1 -Out build\renoise-mgb.wav [-Plugin RetroPlug] [-Bpm 120] ...

Renoise, unlike Reaper, has no render CLI - so the driver is a tool (an .xrnx) that runs
inside Renoise and does the work through the Lua API. This script installs that tool, writes
a job spec, launches Renoise, and waits for the tool's result file. See
tools/renoise/com.retroplug.harness.xrnx/main.lua for the other half.

The tool is INSTALLED (copied) into the user's Renoise Tools directory rather than referenced
in place: Renoise only loads tools from that directory, and there is no search-path override.
The copy is refreshed on every run so an edit to main.lua takes effect without a manual
reinstall - and it is namespaced com.retroplug.harness, so it cannot collide with the user's
own tools.

The tool no-ops unless RETROPLUG_RENOISE_JOB is set, so having it installed does not disturb
normal interactive Renoise use.

Renoise finds the plugin via its own configured VST path. This script does NOT rewrite
Config.xml: that file is the user's live preferences (Renoise rewrites it wholesale on exit),
and corrupting it to run a test would be a poor trade. Point Renoise at build\bin once, in
Preferences > Plug-ins, and every run here picks the freshly built plugin up.
#>

[CmdletBinding()]
param(
    [ValidateSet('render', 'probe')] [string] $Mode = 'render',
    [string] $Out = 'build\renoise-retroplug.wav',
    [string] $Plugin = 'RetroPlug',
    [int] $Bpm = 120,
    [int] $Lines = 64,
    [int] $Note = 48,
    [int] $NoteLine = 0,
    [int] $SampleRate = 44100,
    [int] $BitDepth = 24,
    [int] $TimeoutSec = 300,
    [string] $RenoiseExe
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$tools = $PSScriptRoot
$repo = Split-Path $tools -Parent

function Get-RenoiseExe {
    if ($RenoiseExe -and (Test-Path $RenoiseExe)) { return $RenoiseExe }
    if ($env:RENOISE_EXE -and (Test-Path $env:RENOISE_EXE)) { return $env:RENOISE_EXE }
    $found = Get-ChildItem (Join-Path $env:ProgramFiles 'Renoise*\Renoise.exe') -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending | Select-Object -First 1
    if ($found) { return $found.FullName }
    throw 'run-renoise-render: Renoise.exe not found (set RENOISE_EXE)'
}

# Renoise keeps per-version user data (V3.5.4, ...). Pick the newest, which is the one the
# installed Renoise.exe will actually read.
function Get-RenoiseUserDir {
    $root = Join-Path $env:APPDATA 'Renoise'
    if (-not (Test-Path $root)) { throw "run-renoise-render: no Renoise user dir at $root - launch Renoise once first" }
    $v = Get-ChildItem $root -Directory | Sort-Object Name -Descending | Select-Object -First 1
    if (-not $v) { throw "run-renoise-render: no versioned dir under $root" }
    return $v.FullName
}

$exe = Get-RenoiseExe
$userDir = Get-RenoiseUserDir
$toolsDir = Join-Path $userDir 'Scripts\Tools'
$srcTool = Join-Path $tools 'renoise\com.retroplug.harness.xrnx'
$dstTool = Join-Path $toolsDir 'com.retroplug.harness.xrnx'

if (-not (Test-Path $srcTool)) { throw "run-renoise-render: harness tool missing at $srcTool" }
New-Item -ItemType Directory -Force -Path $dstTool | Out-Null
Copy-Item (Join-Path $srcTool '*') $dstTool -Recurse -Force
Write-Host "renoise harness: installed tool -> $dstTool"

$outFull = [System.IO.Path]::GetFullPath([System.IO.Path]::Combine((Get-Location).Path, $Out))
New-Item -ItemType Directory -Force -Path (Split-Path $outFull -Parent) | Out-Null
if (Test-Path $outFull) { Remove-Item $outFull -Force }

$jobDir = Join-Path $repo 'build\renoise'
New-Item -ItemType Directory -Force -Path $jobDir | Out-Null
$jobPath = Join-Path $jobDir 'job.spec'
$resultPath = Join-Path $jobDir 'job.result'
if (Test-Path $resultPath) { Remove-Item $resultPath -Force }

@(
    "mode=$Mode"
    "out=$outFull"
    "result=$resultPath"
    "plugin=$Plugin"
    "bpm=$Bpm"
    "lines=$Lines"
    "note=$Note"
    "note_line=$NoteLine"
    "samplerate=$SampleRate"
    "bitdepth=$BitDepth"
) | Set-Content -Path $jobPath -Encoding ASCII

$env:RETROPLUG_RENOISE_JOB = $jobPath
Write-Host "renoise harness: job=$jobPath out=$outFull"

$proc = Start-Process -FilePath $exe -PassThru
try {
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        if (Test-Path $resultPath) { break }
        if ($proc.HasExited) { Start-Sleep -Milliseconds 500; break }  # let a last write land
        Start-Sleep -Milliseconds 500
    }
    if (-not $proc.WaitForExit(15000)) { try { $proc.Kill(); $proc.WaitForExit() } catch {} }

    if (-not (Test-Path $resultPath)) {
        # Renoise reports a disabled render with a MODAL DIALOG, not a Lua return value, so the
        # tool never regains control and all the runner sees is a timeout. Name the real cause
        # from the log rather than leaving it as a mystery hang.
        $log = Join-Path $userDir 'Log.txt'
        if ((Test-Path $log) -and (Select-String -Path $log -Pattern 'Rendering is disabled in the demo version' -Quiet)) {
            Write-Host 'SKIP: this Renoise is the DEMO - song:render() is disabled, so the render harness cannot run here.' -ForegroundColor Yellow
            Write-Host '      Register Renoise to enable it, or use -Mode probe, which needs no render.' -ForegroundColor Yellow
            exit 2
        }
        Write-Error "renoise harness: no result after $TimeoutSec s - Renoise never reached the render (tool not loaded, or a modal dialog is blocking). See $log"
        exit 1
    }
    $result = Get-Content $resultPath -Raw
    Write-Host "renoise result:"
    Write-Host $result.Trim()
    if ($result -match '^error') { Write-Error 'renoise harness: the tool reported an error'; exit 1 }
    if ($Mode -eq 'render') {
        if (-not (Test-Path $outFull)) { Write-Error "renoise harness: reported ok but $outFull is missing"; exit 1 }
        Write-Host "rendered: $outFull ($((Get-Item $outFull).Length) bytes)" -ForegroundColor Green
    } else {
        Write-Host 'PASS: Renoise instantiated the plugin and reported its parameter surface.' -ForegroundColor Green
    }
    exit 0
}
finally {
    $env:RETROPLUG_RENOISE_JOB = $null
    if (-not $proc.HasExited) { try { $proc.Kill() } catch {} }
}
