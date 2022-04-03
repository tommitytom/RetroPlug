if "%RETROPLUG_VERSION%"=="" exit /b 1

echo Deploying RetroPlug "%RETROPLUG_VERSION%"

mkdir build\deploy

premake5 vs2019
msbuild build/vs2019/ScriptCompiler.vcxproj /property:Configuration=Release /property:Platform=x64 /m
msbuild build/vs2019/generator.vcxproj /property:Configuration=Release /property:Platform=x64 /m
if %errorlevel% neq 0 exit /b %errorlevel%

premake5 vs2019

REM msbuild build/vs2019/RetroPlugAll.sln /property:Configuration=Release /property:Platform=x64 /m
msbuild build/vs2019/RetroPlugApp.vcxproj /property:Configuration=Release /property:Platform=x64 /m
if %errorlevel% neq 0 exit /b %errorlevel%

copy build\vs2019\bin\x64\Release\RetroPlug_app_x64.exe build\deploy\RetroPlug-%RETROPLUG_VERSION%.exe
REM copy build\vs2019\bin\x64\Release\RetroPlug_vst2_x64.dll "build\deploy\RetroPlug %RETROPLUG_VERSION% (64bit).dll"

cd build\deploy
7z a RetroPlug_standalone-win64-%RETROPLUG_VERSION%.zip RetroPlug-%RETROPLUG_VERSION%.exe
REM 7z a RetroPlug_vst2-win64-%RETROPLUG_VERSION%.zip "RetroPlug %RETROPLUG_VERSION% (64bit).dll"

cd ..\..
