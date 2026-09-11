<#
Shared bring-up / tear-down for the Windows Reaper harness - DOT-SOURCED, not executed.
The Windows counterpart of tools/reaper-env.sh; same contract, far less scaffolding.

Linux needs a whole stack to get Reaper headless (Xvfb + openbox + a dummy JACK server +
an xdotool daemon that dismisses the fresh-config EULA). On Windows none of that applies,
and each piece has a direct replacement:

  Xvfb / openbox    -> nothing. There is a real desktop; Reaper opens a window on it.
  jackd -d dummy    -> Reaper's own "Dummy Audio" device: [audioconfig] mode=4, with
                       dummy_srate / dummy_blocksize. dummy_blocksize IS the render block
                       size, i.e. the exact analogue of jackd's -p period, which is what
                       REAPER_JACK_PERIOD sets on Linux.
  xdotool dismisser -> seeding the [nag] key from the user's own REAPER.ini. That key is the
                       accepted-licence acknowledgement; with it present no EULA appears, so
                       there is no dialog to dismiss. Without it Reaper blocks on the EULA
                       before it ever runs the ReaScript, and the run hangs with nothing on
                       stdout to say why.
  HOME=<cfg dir>    -> -cfgfile <cfg>\reaper.ini. On Windows that relocates the WHOLE resource
                       tree (Scripts/, UserPlugins/, the plugin-scan cache, ...) next to the
                       ini, not just the ini itself - which is what makes the isolation real.

Isolation is keyed off RP_JOB_TAG exactly as on Linux, so each scenario gets its own config
dir and its own warm plugin-scan cache. NOTE the one real difference: jobs are NOT safely
concurrent here. Linux gets parallelism from a per-job X display and JACK server; on Windows
there is a single desktop, and Reaper's plugin windows and audio device are process-global.
Run Windows scenarios sequentially.

Contract for a dot-sourcing script:
    $env:RP_JOB_TAG = 'my-tag'          # REQUIRED before Enter-ReaperEnv
    . "$PSScriptRoot\reaper-env.ps1"
    $rp = Enter-ReaperEnv               # returns a hashtable of paths; throws on failure
    try { ... Start-Reaper / judge the result ... } finally { Exit-ReaperEnv }

Enter-ReaperEnv returns @{ Cfg; Ini; LogDir; Exe; Repo; Tools } and also sets
$env:REAPER_CFG / $env:RP_LOG_DIR for child processes.

Optional knobs (env, read at Enter-ReaperEnv):
    RETROPLUG_VST3_NAME   which built VST3 to host (default: retroplug)
    REAPER_JACK_PERIOD    render block size -> dummy_blocksize (default: 1024). Named after
                          the Linux knob so a scenario sets ONE variable for both platforms.
    RP_SCAN_FRESH=1       drop the plugin-scan cache first (the editor checks want a cold scan)
    RP_WANT_CLAP=1        also expose the built .clap to Reaper
    REAPER_EXE            override the Reaper executable path
#>

Set-StrictMode -Version Latest

$script:RpReaperProc = $null

function Get-ReaperExe {
    if ($env:REAPER_EXE -and (Test-Path $env:REAPER_EXE)) { return $env:REAPER_EXE }
    $candidates = @(
        (Join-Path $env:ProgramFiles 'REAPER (x64)\reaper.exe'),
        (Join-Path $env:ProgramFiles 'REAPER\reaper.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'REAPER\reaper.exe')
    )
    foreach ($c in $candidates) { if (Test-Path $c) { return $c } }
    throw 'reaper-env: reaper.exe not found (set REAPER_EXE)'
}

# The accepted-licence acknowledgement from the user's own install. Without it a fresh config
# stops at the EULA and never reaches the ReaScript. Returns '' when there is no user config,
# in which case the caller is told plainly rather than left waiting on an invisible dialog.
function Get-ReaperNagLine {
    $src = Join-Path $env:APPDATA 'REAPER\REAPER.ini'
    if (-not (Test-Path $src)) { return '' }
    $m = Select-String -Path $src -Pattern '^nag=' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($m) { return $m.Line.Trim() }
    return ''
}

function Enter-ReaperEnv {
    if (-not $env:RP_JOB_TAG) { throw 'reaper-env: RP_JOB_TAG must be set before Enter-ReaperEnv' }

    $tools = $PSScriptRoot
    $repo = Split-Path $tools -Parent
    $exe = Get-ReaperExe

    $vst3Name = if ($env:RETROPLUG_VST3_NAME) { $env:RETROPLUG_VST3_NAME } else { 'retroplug' }
    $binDir = Join-Path $repo 'build\bin'
    $vst3Bundle = Join-Path $binDir ($vst3Name + '.vst3')
    if (-not (Test-Path $vst3Bundle)) { throw "reaper-env: $vst3Bundle not built" }

    $tagSafe = ($env:RP_JOB_TAG -replace '[^A-Za-z0-9_-]', '_')
    $cfg = Join-Path $repo ('build\reaper-cfg-win-' + $tagSafe)
    $logDir = Join-Path $cfg 'logs'
    New-Item -ItemType Directory -Force -Path $logDir | Out-Null

    if ($env:RP_SCAN_FRESH -eq '1') {
        Get-ChildItem $cfg -Filter 'reaper-vstplugins*.ini*' -ErrorAction SilentlyContinue |
            ForEach-Object { Remove-Item $_.FullName -Force -ErrorAction SilentlyContinue }
    }

    $nag = Get-ReaperNagLine
    if (-not $nag) {
        Write-Warning 'reaper-env: no [nag] key in %APPDATA%\REAPER\REAPER.ini - a fresh config may stop at the EULA and hang. Launch Reaper once interactively first.'
    }

    $period = if ($env:REAPER_JACK_PERIOD) { $env:REAPER_JACK_PERIOD } else { '1024' }

    # Reaper scans every directory in vstpath64 for .vst3 bundles; pointing it straight at
    # build/bin is the Windows equivalent of symlinking the bundle into ~/.vst3.
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add('[reaper]')
    $lines.Add("vstpath64=$binDir")
    if ($env:RP_WANT_CLAP -eq '1') {
        $clap = Join-Path $binDir ($vst3Name + '.clap')
        if (-not (Test-Path $clap)) { throw "reaper-env: $clap not built (RP_WANT_CLAP=1)" }
        $lines.Add("clappath64=$binDir")
    }
    $lines.Add('vstfullstate=49989')
    $lines.Add('renderclosewhendone=4')
    $lines.Add('[nag]')
    $lines.Add($nag)
    $lines.Add('[audioconfig]')
    # mode=4 is Dummy Audio: no hardware, no device-open error dialog, and a block size we
    # control. Verified against GetAudioDeviceInfo on this machine - 0 and 6 report no device
    # at all, 1=DirectSound, 2=WaveOut, 3=ASIO, 5=WASAPI.
    $lines.Add('mode=4')
    $lines.Add('dummy_srate=44100')
    $lines.Add("dummy_blocksize=$period")

    $ini = Join-Path $cfg 'reaper.ini'
    Set-Content -Path $ini -Value $lines -Encoding ASCII

    $env:REAPER_CFG = $cfg
    $env:RP_LOG_DIR = $logDir

    Write-Host "reaper-env[$env:RP_JOB_TAG]: cfg=$cfg blocksize=$period"

    return @{ Cfg = $cfg; Ini = $ini; LogDir = $logDir; Exe = $exe; Repo = $repo; Tools = $tools }
}

# Launch Reaper against the isolated config. Returns the Process so the caller can wait on it.
# Stdout/stderr are deliberately not redirected: reaper.exe is a GUI-subsystem binary and
# writes nothing useful to them.
function Start-Reaper {
    param(
        [Parameter(Mandatory)] [hashtable] $Rp,
        [Parameter(Mandatory)] [string[]] $ReaperArgs
    )
    $all = @('-cfgfile', $Rp.Ini, '-nosplash') + $ReaperArgs
    $script:RpReaperProc = Start-Process -FilePath $Rp.Exe -ArgumentList $all -PassThru
    return $script:RpReaperProc
}

# Wait for Reaper to exit on its own, killing it past the deadline. Returns its exit code, or
# $null when it had to be killed.
function Wait-Reaper {
    param([System.Diagnostics.Process] $Proc, [int] $TimeoutSec = 300)
    if ($Proc.WaitForExit($TimeoutSec * 1000)) { return $Proc.ExitCode }
    Write-Warning "reaper-env: Reaper still running after $TimeoutSec s - killing"
    try { $Proc.Kill(); $Proc.WaitForExit() } catch {}
    return $null
}

# Wait until a file exists and its size settles across two reads. The Linux twin polls a plugin
# snapshot PNG; the same shape works for any artifact Reaper writes asynchronously.
function Wait-SettledFile {
    param([Parameter(Mandatory)][string] $Path, [int] $TimeoutSec = 120)
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $last = -1
    while ((Get-Date) -lt $deadline) {
        if (Test-Path $Path) {
            $cur = (Get-Item $Path).Length
            if ($cur -gt 0 -and $cur -eq $last) { return $true }
            $last = $cur
        }
        Start-Sleep -Milliseconds 500
    }
    return (Test-Path $Path)
}

function Exit-ReaperEnv {
    if ($script:RpReaperProc -and -not $script:RpReaperProc.HasExited) {
        try { $script:RpReaperProc.Kill(); $script:RpReaperProc.WaitForExit() } catch {}
    }
    $script:RpReaperProc = $null
}
