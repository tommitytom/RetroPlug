#!/usr/bin/env powershell
param
(
    [ValidateSet('debug', 'debug-asan', 'development', 'release')][string] $BuildType = "development",
    [Parameter(HelpMessage="Runs premake before building, useful when changing premake files")]
    [switch]$Configure = $false,
    [Parameter(HelpMessage="Adds some debug flags to any configuration, which may give more insight on memory issues, but slow down compilation and linking")]
    [switch]$ExtraDebug = $false
)

if ($ExtraDebug)
{
    $Configure = $true
}

$EMSCRIPTEN_VERSION = "4.0.13"

function InstallEmscripten() {

    Write-Host "`nInstall Emscripten toolchain version $EMSCRIPTEN_VERSION...`n" -ForegroundColor DarkMagenta

    .\thirdparty\emsdk\emsdk.ps1 update # Fetch all versions to make sure install can find the correct version.
    .\thirdparty\emsdk\emsdk.ps1 install $EMSCRIPTEN_VERSION

    if (!$?) {
        throw "Installing Emscripten failed: '$LASTEXITCODE'."
    }
}

function ActivateEmscripten() {

    Write-Host "`nActivating Emscripten toolchain version $EMSCRIPTEN_VERSION...`n" -ForegroundColor DarkMagenta

    .\thirdparty\emsdk\emsdk.ps1 activate $EMSCRIPTEN_VERSION

    if (!$?) {
        throw "Activating Emscripten failed: '$LASTEXITCODE'."
    }
}

function GenerateSolution() {

    Write-Host "`nGenerating Game Solution...`n" -ForegroundColor DarkMagenta

    $extraFlags = if ($ExtraDebug) { "--em-extra-debug" } else { "" }

    .\thirdparty\Framework\thirdparty\bin\premake5 --emscripten gmake $extraFlags

    if (!$? -or ($LastExitCode -ne 0)) {
        throw "Generating Game Solution failed: '$LASTEXITCODE'."
    }
}

function Compile([string] $Type) {

    Write-Host "`nCompiling Game for $Type...`n" -ForegroundColor DarkMagenta

    New-Item -Path "$PSScriptRoot\build\gmake" -Name "$Type" -ItemType Directory -Force | Out-Null

    Push-Location "$PSScriptRoot\build\gmake"

    $cores = (Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors

    ..\..\thirdparty\bin\make.exe "-j$cores" "RetroPlug-app" config=$Type"_emscripten"
    #..\..\thirdparty\bin\make.exe "RetroPlug-app" config=$Type"_emscripten"

    Pop-Location

    if (!$? -or ($LastExitCode -ne 0)) {
        throw "Compiling Game failed: '$LASTEXITCODE'."
    }
}

function PatchWindowsMakefiles() {
    # On Windows mkdir returns an error when a path already exists.
    # That causes build errors because makefiles have directory targets that "build" with mkdir,
    # but, in multi-threaded environment, the directory can be created by a different thread
    # between the target check and the mkdir. To work around that, we add - before mkdir to ignore
    # its exit code.

    $makeFiles = Get-ChildItem ./build/gmake *.make -rec

    foreach ($file in $makeFiles) {
        (Get-Content $file.PSPath) |
        Foreach-Object { $_ -replace ' mkdir \$', ' -mkdir $' } |
        Set-Content $file.PSPath
    }
}

function CopyWebTemplate([string] $Type) {
    Copy-Item "$PSScriptRoot\webtemplate\tick-worker.js" -Destination "$PSScriptRoot\build\gmake\game\$Type\tick-worker.js" -Force
    if (!$?) {
        throw "Copy Web Template (tick-worker.js) failed: '$LASTEXITCODE'."
    }
}

# force the current working directory to the directory where the script is located
Push-Location $PSScriptRoot

InstallEmscripten
ActivateEmscripten

if ($Configure) {
    GenerateSolution
    PatchWindowsMakefiles
}

# Build Game
Compile $BuildType
#CopyWebTemplate $BuildType

# restore the original working directory
Pop-Location

Write-Host "`nWeb build finished successfully.`n" -ForegroundColor Green
