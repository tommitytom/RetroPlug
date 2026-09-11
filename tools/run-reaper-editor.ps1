<#
Float the hosted RetroPlug editor in Reaper and assert it actually rendered - the Windows
counterpart of tools/run-reaper-editor.sh.

    tools\run-reaper-editor.ps1 [path\to\editor.lua]

This is the only check of ON-SCREEN editor rendering. It guards the editor <-> control-plane
single-store graph: a blank editor means the UI composed its own store, or the class-id counter
desynced and the DAW-hosted editor never got a surface to draw on.

The verdict is the plugin's OWN LVGL snapshot (RETROPLUG_SCREENSHOT_PATH, rewritten every
interval), not a screen grab. A blank editor still writes a PNG - just a tiny one (~159 B) - so
the check is on the file's SIZE, which is why a byte threshold rather than mere existence is the
pass condition.

The bash twin also grabs the X display with ffmpeg for eyeballing. That is explicitly not the
authoritative check, and there is no x11grab here, so it is simply dropped rather than
reimplemented with a Windows screen-capture dependency.

Note RP_SCAN_FRESH=1: the editor checks want a COLD plugin scan, because a warm cache hides the
multi-runtime class-id hazard this test exists to catch.

Unlike the render checks this one needs a real interactive desktop session - it opens windows.
It cannot run from a service or an SSH session with no desktop.
#>

[CmdletBinding()]
param(
    [Parameter(Position = 0)] [string] $Lua,
    [int] $TimeoutSec = 90
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. "$PSScriptRoot\reaper-env.ps1"

if (-not $Lua) { $Lua = Join-Path $PSScriptRoot 'reaper-editor-open.lua' }
if (-not (Test-Path $Lua)) { throw "run-reaper-editor: lua not found: $Lua" }
$luaFull = (Resolve-Path $Lua).Path

$repo = Split-Path $PSScriptRoot -Parent
$out = if ($env:RP_EDITOR_OUT) { $env:RP_EDITOR_OUT } else { Join-Path $repo 'build\reaper-editor-lvgl.png' }
$minBytes = if ($env:RP_EDITOR_MIN_BYTES) { [int]$env:RP_EDITOR_MIN_BYTES } else { 1000 }

if (-not $env:RP_JOB_TAG) { $env:RP_JOB_TAG = 'editor' }
$env:RP_SCAN_FRESH = '1'

$rp = Enter-ReaperEnv
Push-Location $rp.Repo
try {
    if (Test-Path $out) { Remove-Item $out -Force }
    Write-Host "reaper editor: out=$out lua=$luaFull"

    $env:RETROPLUG_SCREENSHOT_PATH = $out
    $env:RETROPLUG_SCREENSHOT_INTERVAL_MS = '500'

    $proc = Start-Reaper -Rp $rp -ReaperArgs @($luaFull)
    $settled = Wait-SettledFile -Path $out -TimeoutSec $TimeoutSec
    Exit-ReaperEnv

    if (-not (Test-Path $out)) {
        Write-Host "FAIL: no LVGL snapshot written ($out) - the editor never rendered." -ForegroundColor Red
        exit 1
    }
    $bytes = (Get-Item $out).Length
    if ($bytes -le $minBytes) {
        Write-Host "FAIL: LVGL snapshot is only ${bytes}B (<= ${minBytes}B) - the editor is blank." -ForegroundColor Red
        exit 1
    }
    Write-Host "PASS: editor rendered - LVGL snapshot $out is ${bytes}B (blank would be ~159B)." -ForegroundColor Green
    exit 0
}
finally {
    $env:RETROPLUG_SCREENSHOT_PATH = $null
    $env:RETROPLUG_SCREENSHOT_INTERVAL_MS = $null
    $env:RP_SCAN_FRESH = $null
    Pop-Location
    Exit-ReaperEnv
}
