local GAINPUT_DIR = "../thirdparty/gainput"

project "gainput"
	kind "StaticLib"

	includedirs { GAINPUT_DIR .. "/lib/include" }
	files { GAINPUT_DIR .. "/lib/include/gainput/**.h", GAINPUT_DIR .. "/lib/source/gainput/**.cpp" }

	disablewarnings { "4267", "4244" }