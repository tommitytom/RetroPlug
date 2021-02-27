local GAINPUT_DIR = "../thirdparty/gainput"

project "gainput"
	kind "StaticLib"

	sysincludedirs { GAINPUT_DIR .. "/lib/include" }
	files { GAINPUT_DIR .. "/lib/include/gainput/**.h", GAINPUT_DIR .. "/lib/source/gainput/**.cpp" }

	configuration { "windows" }
		disablewarnings { "4267", "4244" }
		