local SCRIPTS_DIR = debug.getinfo(1, "S").source:sub(2):match("(.*/)")
SCRIPTS_DIR = string.gsub(SCRIPTS_DIR, "scripts/", "")

project "configure"
	kind "Utility"
	dependson "generator"

	files { "../premake5.lua" }

	configuration { "windows" }
		prebuildcommands {
			"cd " .. SCRIPTS_DIR,
			"premake5 vs2019"
		}
