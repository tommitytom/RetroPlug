<#
Check that the DAW re-reads RetroPlug's parameter names when a ROM is loaded - the Windows
counterpart of tools/run-reaper-params.sh.

    tools\run-reaper-params.ps1                          # vst3, mouse-driven "Load mGB"
    $env:RP_PARAMS_FORMAT='clap'; tools\run-reaper-params.ps1
    $env:RP_PARAMS_ROM='resources\roms\bliptoaster-vrc7.nes'; tools\run-reaper-params.ps1

Two paths, and they differ in how the ROM gets into the plugin:

  RP_PARAMS_ROM set  - the ROM is autoloaded via a generated .rplg, so the run is fully
                       deterministic. No input synthesis. Prefer this path.
  otherwise          - the ROM arrives by CLICKING "Load mGB" in the plugin's own editor, so
                       the check also covers the editor's load flow. The bash twin drives that
                       with xdotool; here it is SetCursorPos + mouse_event through P/Invoke,
                       aimed at the FX window found by title.

The mouse path needs a real interactive desktop with nothing overlapping the FX window. It is
inherently the more fragile of the two - same as on Linux, which is why it retries the click.

Exit codes match the bash twin: 0 pass, 1 fail, 2 skip (nothing to check / wrong plugin loaded).
#>

[CmdletBinding()]
param([int] $TimeoutSec = 180)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. "$PSScriptRoot\reaper-env.ps1"

$repo = Split-Path $PSScriptRoot -Parent
$format = if ($env:RP_PARAMS_FORMAT) { $env:RP_PARAMS_FORMAT } else { 'vst3' }
$fxToken = switch ($format) {
    'clap' { 'CLAPi' }
    'vst3' { 'VST3i' }
    default { throw "run-reaper-params: unknown RP_PARAMS_FORMAT '$format' (want vst3 or clap)" }
}
$expectLanes = if ($env:RP_PARAMS_EXPECT_LANES) { $env:RP_PARAMS_EXPECT_LANES }
               else { 'Pulse 1 Duty;FM 1 Volume;FM 6 Volume;FM Attack;FM Key-Scale Rate;FM 3 LFO Shape;FM 3 Tremolo Depth' }
$gridMaxBytes = if ($env:RP_GRID_MAX_BYTES) { [int]$env:RP_GRID_MAX_BYTES } else { 2500 }
$loadXOff = if ($env:RP_LOAD_X_OFF) { [int]$env:RP_LOAD_X_OFF } else { 135 }
$loadYOff = if ($env:RP_LOAD_Y_OFF) { [int]$env:RP_LOAD_Y_OFF } else { 110 }

if (-not $env:RP_JOB_TAG) { $env:RP_JOB_TAG = "params-$format" }
if ($format -eq 'clap') { $env:RP_WANT_CLAP = '1' }
$env:RP_PARAMS_FX = "${fxToken}: RetroPlug"

# Win32 just enough to find the FX window and click inside it. xdotool's Windows equivalent is
# not a tool but these four calls.
if (-not ('RpWin32' -as [type])) {
    Add-Type -Namespace Rp -Name Win32 -MemberDefinition @'
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string c, string n);
[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
[DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
[DllImport("user32.dll")] public static extern void mouse_event(uint f, uint x, uint y, uint d, UIntPtr i);
public struct RECT { public int Left, Top, Right, Bottom; }
'@ -UsingNamespace System.Runtime.InteropServices -ErrorAction SilentlyContinue
}
$MOUSEEVENTF_LEFTDOWN = 0x0002; $MOUSEEVENTF_LEFTUP = 0x0004

$rp = Enter-ReaperEnv
Push-Location $rp.Repo
try {
    $sigDir = $rp.LogDir
    $snap = Join-Path $repo "build\reaper-params-$format.png"
    $out = Join-Path $sigDir 'rp-params.txt'
    $env:RP_PARAMS_SIGDIR = $sigDir

    foreach ($f in @($out, $snap, (Join-Path $sigDir 'rp-params-before'), (Join-Path $sigDir 'rp-params-loaded'), (Join-Path $sigDir 'rp-params-done'))) {
        if (Test-Path $f) { Remove-Item $f -Force }
    }

    $romMode = $false
    if ($env:RP_PARAMS_ROM) {
        if (-not (Test-Path $env:RP_PARAMS_ROM)) {
            Write-Host "SKIP: $env:RP_PARAMS_ROM not present, nothing to check."
            exit 2
        }
        $romAbs = (Resolve-Path $env:RP_PARAMS_ROM).Path
        $rplg = Join-Path $sigDir 'params.rplg'
        $json = '{"schemaVersion":"4","settings":{},"systems":[{"platform":"nes","core":"mesen","romPath":"' +
                ($romAbs -replace '\\', '\\\\') + '"}]}'
        Set-Content -Path $rplg -Value $json -Encoding ASCII
        $env:RETROPLUG_AUTOLOAD_PROJECT = $rplg
        $env:RP_PARAMS_AUTOLOAD = '1'
        $romMode = $true
    }

    $env:RETROPLUG_SCREENSHOT_PATH = $snap
    $env:RETROPLUG_SCREENSHOT_INTERVAL_MS = '400'

    $proc = Start-Reaper -Rp $rp -ReaperArgs @((Join-Path $PSScriptRoot 'reaper-params.lua'))

    [void](Wait-SettledFile -Path (Join-Path $sigDir 'rp-params-before') -TimeoutSec 60)
    [void](Wait-SettledFile -Path $snap -TimeoutSec 45)

    if (-not $romMode) {
        # Find the floating FX window and click "Load mGB" at the same offset the bash twin uses.
        $hwnd = [Rp.Win32]::FindWindow($null, "CLAPi: RetroPlug")
        if ($hwnd -eq [IntPtr]::Zero) { $hwnd = [Rp.Win32]::FindWindow($null, "VST3i: RetroPlug") }
        $x = 114 + $loadXOff; $y = 100 + $loadYOff
        if ($hwnd -ne [IntPtr]::Zero) {
            $r = New-Object Rp.Win32+RECT
            if ([Rp.Win32]::GetWindowRect($hwnd, [ref]$r)) { $x = $r.Left + $loadXOff; $y = $r.Top + $loadYOff }
            [void][Rp.Win32]::SetForegroundWindow($hwnd)
        }
        Write-Host "run-reaper-params[$format]: fxwnd=$hwnd click 'Load mGB' at ($x,$y)"

        $tries = if ($env:RP_PARAMS_CLICK_TRIES) { [int]$env:RP_PARAMS_CLICK_TRIES } else { 12 }
        $loadedSz = 0
        for ($attempt = 1; $attempt -le $tries; $attempt++) {
            [void][Rp.Win32]::SetCursorPos($x, $y); Start-Sleep -Milliseconds 400
            [Rp.Win32]::mouse_event($MOUSEEVENTF_LEFTDOWN, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 400
            [Rp.Win32]::mouse_event($MOUSEEVENTF_LEFTUP, 0, 0, 0, [UIntPtr]::Zero)
            for ($i = 0; $i -lt 16; $i++) {
                Start-Sleep -Milliseconds 500
                $loadedSz = if (Test-Path $snap) { (Get-Item $snap).Length } else { 0 }
                if ($loadedSz -gt 0 -and $loadedSz -le $gridMaxBytes) { break }
            }
            if ($loadedSz -gt 0 -and $loadedSz -le $gridMaxBytes) { break }
            Write-Host "run-reaper-params[$format]: load-click attempt $attempt didn't register (snapshot ${loadedSz}B), retrying..."
        }
        New-Item -ItemType File -Force -Path (Join-Path $sigDir 'rp-params-loaded') | Out-Null
    }

    [void](Wait-SettledFile -Path (Join-Path $sigDir 'rp-params-done') -TimeoutSec 60)
    Exit-ReaperEnv

    Copy-Item $out (Join-Path $repo "build\reaper-params-$format.txt") -Force -ErrorAction SilentlyContinue

    if (-not (Test-Path $out) -or (Get-Item $out).Length -eq 0) {
        Write-Host "SKIP: no parameter dump written - see $sigDir"
        exit 2
    }
    $lines = Get-Content $out
    $fxLoaded = ($lines | Where-Object { $_ -match '^#fx\t' } | Select-Object -First 1) -replace '^#fx\t', ''
    if ($fxLoaded -notlike "$fxToken*") {
        Write-Host "SKIP: Reaper loaded '$fxLoaded', not a $fxToken plugin."
        exit 2
    }
    $claimed = @($lines | Where-Object { $_ -match "^after\t\d+\t1: " -and $_ -notmatch "^after\t\d+\t1: CC \d+$" }).Count
    Write-Host "run-reaper-params[$format]: fx='$fxLoaded' claimed=$claimed lanes"

    $fail = $false
    foreach ($want in ($expectLanes -split ';')) {
        if (-not $want) { continue }
        if (-not ($lines | Where-Object { $_.Contains("`t1: $want") })) {
            Write-Host "FAIL: the host does not report '1: $want'." -ForegroundColor Red
            $fail = $true
        }
    }
    if ($fail) { exit 1 }
    Write-Host "PASS: the host reports this build's lanes ($claimed claimed)." -ForegroundColor Green
    exit 0
}
finally {
    foreach ($v in 'RETROPLUG_SCREENSHOT_PATH','RETROPLUG_SCREENSHOT_INTERVAL_MS','RP_PARAMS_SIGDIR','RP_PARAMS_FX','RP_WANT_CLAP','RP_PARAMS_AUTOLOAD','RETROPLUG_AUTOLOAD_PROJECT') {
        Set-Item -Path "env:$v" -Value $null -ErrorAction SilentlyContinue
    }
    Pop-Location
    Exit-ReaperEnv
}
