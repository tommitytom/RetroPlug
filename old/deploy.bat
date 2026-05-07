if "%RETROPLUG_VERSION%"=="" exit /b 1

echo Deploying RetroPlug "%RETROPLUG_VERSION%"

mkdir build\deploy

REM Start by building the lua script compiler, and using it to compile lua scripts to C++
premake5 vs2022
msbuild build/vs2022/ScriptCompiler.vcxproj /property:Configuration=Release /property:Platform=x64 /m
msbuild build/vs2022/generator.vcxproj /property:Configuration=Release /property:Platform=x64 /m
if %errorlevel% neq 0 exit /b %errorlevel%

REM Run premake again to detect newly generated files
premake5 vs2022

REM Build the standalone app
msbuild build/vs2022/RetroPlug-app.vcxproj /property:Configuration=Release /property:Platform=x64 /m
msbuild build/vs2022/RetroPlug-iPlug2-vst2.vcxproj /property:Configuration=Release /property:Platform=x64 /m
msbuild build/vs2022/RetroPlug-vst3.vcxproj /property:Configuration=Release /property:Platform=x64 /m
if %errorlevel% neq 0 exit /b %errorlevel%

copy build\vs2022\bin\x64\Release\RetroPlug-app.exe build\deploy\RetroPlug-x64-%RETROPLUG_VERSION%.exe
copy build\vs2022\bin\x64\Release\RetroPlug-iPlug2-vst2.dll "build\deploy\RetroPlug %RETROPLUG_VERSION% (64bit).dll"
copy build\vs2022\bin\x64\Release\RetroPlug-vst3.vst3 "build\deploy\RetroPlug %RETROPLUG_VERSION% (64bit).vst3"

cd build\deploy
7z a RetroPlug_standalone-win64-%RETROPLUG_VERSION%.zip RetroPlug-x64-%RETROPLUG_VERSION%.exe
7z a RetroPlug_vst2-win64-%RETROPLUG_VERSION%.zip "RetroPlug %RETROPLUG_VERSION% (64bit).dll"
7z a RetroPlug_vst3-win64-%RETROPLUG_VERSION%.zip "RetroPlug %RETROPLUG_VERSION% (64bit).vst3"

cd ..\..
