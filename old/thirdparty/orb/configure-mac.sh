#!/bin/bash

./thirdparty/bin/premake5-mac xcode4
#sed -i '' 's+/\* IGraphicsNanoVG_src.m \*/;+/\* IGraphicsNanoVG_src.m \*/; settings = {COMPILER_FLAGS = "-fobjc-arc"; };+g' "build/xcode4/RetroPlug-app.xcodeproj/project.pbxproj"
#sed -i '' 's+lastKnownFileType = sourcecode.cpp.cpp; name = IPlugAPP_main.cpp;+explicitFileType = sourcecode.cpp.objcpp; name = IPlugAPP_main.cpp;+g' "build/xcode4/RetroPlug-app.xcodeproj/project.pbxproj"
