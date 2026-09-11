<#
Author a Reaper project from scratch by running a ReaScript inside the isolated Windows
harness (tools/reaper-env.ps1). The counterpart of tools/run-reaper-author.sh - same
arguments, same env contract, and it runs the SAME .lua author scripts unchanged.

Usage:
    tools\run-reaper-author.ps1 OUTPUT.RPP RENDER_DIR AUTHOR.lua [FIXTURE.rplg|.rplg.zip]

OUTPUT.RPP     where the lua writes the project (REAPER_AUTHOR_DEST)
RENDER_DIR     dir for the project's render output (REAPER_AUTHOR_RENDER_DIR; the lua stamps
               it into RENDER_FILE). Absolutised, because Reaper resolves a relative
               RENDER_FILE against the .rpp's own directory rather than the repo root, which
               would scatter renders next to the fixtures.
AUTHOR.lua     ReaScript that builds + saves the project
FIXTURE        optional .rplg / .rplg.zip that becomes RETROPLUG_AUTOLOAD_PROJECT, so the
               saved .RPP captures a configured RetroPlug state

Isolation is keyed off RP_JOB_TAG (default: author-<output basename>).

Unlike the bash twin this waits for a clean exit first. The Linux script cannot: its Reaper
has no UI thread, so Main_OnCommand(40004) frequently never returns and it has to kill Reaper
once the .rpp settles. Here there IS a real UI thread and the quit does return - but the
settle-poll is kept as the fallback path, because the lua saves synchronously BEFORE asking to
quit, so a hung quit must still not fail a run whose artifact is already on disk.
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)] [string] $Dest,
    [Parameter(Mandatory, Position = 1)] [string] $RenderDir,
    [Parameter(Mandatory, Position = 2)] [string] $AuthorLua,
    [Parameter(Position = 3)] [string] $Fixture,
    [int] $TimeoutSec = 300
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. "$PSScriptRoot\reaper-env.ps1"

if (-not (Test-Path $AuthorLua)) { throw "error: author lua script not found: $AuthorLua" }
$authorLuaFull = (Resolve-Path $AuthorLua).Path

$env:REAPER_AUTHOR_DEST = $Dest
$env:REAPER_AUTHOR_RENDER_DIR = [System.IO.Path]::GetFullPath(
    [System.IO.Path]::Combine((Get-Location).Path, $RenderDir))

if ($Fixture) {
    if (-not (Test-Path $Fixture)) { throw "error: fixture not found: $Fixture" }
    if ($Fixture -notmatch '\.rplg(\.zip)?$') { throw "error: fixture must be a .rplg or .rplg.zip: $Fixture" }
    $env:RETROPLUG_AUTOLOAD_PROJECT = (Resolve-Path $Fixture).Path
    Write-Host "fixture (rplg): $env:RETROPLUG_AUTOLOAD_PROJECT"
}

if (-not $env:RP_JOB_TAG) {
    $env:RP_JOB_TAG = 'author-' + [System.IO.Path]::GetFileNameWithoutExtension($Dest)
}

$rp = Enter-ReaperEnv
Push-Location $rp.Repo
try {
    # The .rpp fixtures are gitignored (derived) and git cannot track an empty dir, so on a fresh
    # checkout the destination directory does not exist - Main_SaveProjectEx would then silently
    # write nothing. Create it, and drop any prior artifact so the settle-poll keys on THIS run.
    $destFull = [System.IO.Path]::GetFullPath([System.IO.Path]::Combine($rp.Repo, $Dest))
    New-Item -ItemType Directory -Force -Path (Split-Path $destFull -Parent) | Out-Null
    if (Test-Path $destFull) { Remove-Item $destFull -Force }

    Write-Host "reaper author: dest=$destFull lua=$authorLuaFull"
    Write-Host "  renderDir=$env:REAPER_AUTHOR_RENDER_DIR config=$($rp.Cfg)"

    $proc = Start-Reaper -Rp $rp -ReaperArgs @($authorLuaFull)

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $last = -1
    while (-not $proc.HasExited -and (Get-Date) -lt $deadline) {
        if (Test-Path $destFull) {
            $sz = (Get-Item $destFull).Length
            if ($sz -gt 0 -and $sz -eq $last) { break }  # saved and settled
            $last = $sz
        }
        Start-Sleep -Seconds 1
    }
    Exit-ReaperEnv

    if (Test-Path $destFull) {
        Write-Host "authored: $destFull ($((Get-Item $destFull).Length) bytes)"
        exit 0
    }
    Write-Error "error: $destFull was not produced"
    exit 1
}
finally {
    Pop-Location
    Exit-ReaperEnv
}
