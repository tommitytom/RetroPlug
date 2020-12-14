local SFW_DIR = "../thirdparty/simplefilewatcher"

project "simplefilewatcher"
	kind "StaticLib"

	includedirs { SFW_DIR .. "/include" }
	files { SFW_DIR .. "/include/**.h", SFW_DIR .. "/source/**.cpp" }