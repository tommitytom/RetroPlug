<#
Render a Reaper project through the isolated Windows harness (tools/reaper-env.ps1).
The Windows counterpart of tools/run-reaper-render.sh - same arguments, same contract,
same .rpp fixtures and the same verdict from tools/reaper-timing-analyze.py.

Usage:
    tools\run-reaper-render.ps1 PROJECT.RPP [AUTOLOAD.RPLG]

PROJECT.RPP    reaper project to render. RENDER_FILE / RENDER_PATTERN inside the project
               decides where the WAV lands; this script sets the working directory to the
               repo root so the project's relative paths resolve there, exactly as the bash
               version does.
AUTOLOAD.RPLG  optional; exported as RETROPLUG_AUTOLOAD_PROJECT so the plugin applies a
               preconfigured project at construction. Absolutised FIRST - the plugin's
               readFile resolves against the process cwd, and Reaper changes its cwd when it
               loads a project, so a relative path would not resolve at construction time.

Isolation is keyed off RP_JOB_TAG (default: the project basename). Unlike Linux these jobs
are NOT safe to run concurrently - see the note in reaper-env.ps1.
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)] [string] $Project,
    [Parameter(Position = 1)] [string] $Autoload,
    [int] $TimeoutSec = 1800
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. "$PSScriptRoot\reaper-env.ps1"

if (-not (Test-Path $Project)) { throw "error: project not found: $Project" }
$projectFull = (Resolve-Path $Project).Path

if ($Autoload) {
    if (-not (Test-Path $Autoload)) { throw "error: autoload fixture not found: $Autoload" }
    $env:RETROPLUG_AUTOLOAD_PROJECT = (Resolve-Path $Autoload).Path
}

if (-not $env:RP_JOB_TAG) {
    $env:RP_JOB_TAG = [System.IO.Path]::GetFileNameWithoutExtension($Project)
}

$rp = Enter-ReaperEnv
Push-Location $rp.Repo
try {
    Write-Host "reaper render: project=$projectFull autoload=$(if ($Autoload) { $env:RETROPLUG_AUTOLOAD_PROJECT } else { '(none)' })"
    Write-Host "  config=$($rp.Cfg)"

    $proc = Start-Reaper -Rp $rp -ReaperArgs @('-renderproject', $projectFull)
    $rc = Wait-Reaper -Proc $proc -TimeoutSec $TimeoutSec

    if ($null -eq $rc) {
        Write-Error "reaper render: timed out after $TimeoutSec s"
        exit 1
    }
    Write-Host "reaper exited: $rc"
    exit $rc
}
finally {
    Pop-Location
    Exit-ReaperEnv
}
