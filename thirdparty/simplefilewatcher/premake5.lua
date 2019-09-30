-- A solution contains projects, and defines the available configurations
solution "SimpleFileWatcher"
	location("build/" .. _ACTION)
	targetdir "bin"
	configurations { "Debug", "Release" }

	-- A project defines one build target
	project "SimpleDemo"
		kind "ConsoleApp"
		language "C++"
		files { 
			"source/*.cpp",
			"SimpleDemo.cpp"
		}
		includedirs { "include" }
		objdir "intermediate"
	
	configuration "Debug"
		defines { "DEBUG" }
		flags { "Symbols" }
		objdir "build/intermediate/Debug"
		targetname "SimpleDemo_d"

	configuration "Release"
		defines { "NDEBUG" }
		flags { "Optimize" }
		objdir "build/intermediate/Release"
