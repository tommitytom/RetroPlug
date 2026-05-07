local LIBLSDJ_DIR = "../thirdparty/liblsdj"

project "liblsdj"
	kind "StaticLib"

	includedirs { LIBLSDJ_DIR .. "/liblsdj/include/lsdj" }
	files { LIBLSDJ_DIR .. "/liblsdj/include/lsdj/**.h", LIBLSDJ_DIR .. "/liblsdj/src/**.c" }

	configuration { "windows" }
		disablewarnings { "4334", "4098", "4244" }
