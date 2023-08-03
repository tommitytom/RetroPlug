local util = {}

function util.joinFlags(...)
	local t = ""
	for _, g in ipairs({...}) do
		for _, v in ipairs(g) do
			t = t .. v .. " "
		end
	end

	return t
end

function util.setupWorkspace()
	local buildFolder = _ACTION

	if _OPTIONS["web"] then
		buildFolder = "emscripten"
	end

	local PLATFORMS = { "x86", "x64" }

	if _ACTION == "gmake2" then
		table.insert(PLATFORMS, "Emscripten")
	elseif _ACTION == "xcode4" then
		PLATFORMS = { "x64" }
	end

	location("build/" .. buildFolder)

	configurations { "Debug", "Development", "Release", "Debug-ASAN", "Development-ASAN", "Release-ASAN", "Release-Profiling" }
	platforms (PLATFORMS)
	flags { "MultiProcessorCompile" }
	language "C++"
	characterset "MBCS"
	cppdialect "c++20"
	vectorextensions "SSE2"
	editAndContinue "off"

	filter "configurations:Debug*"
		defines { "_DEBUG", "FW_DEBUG" }
		optimize "Off"
		symbols "Full"
	filter "configurations:Development*"
		defines { "NDEBUG", "FW_DEVELOPMENT" }
		optimize "Full"
		symbols "On"
		inlining "Explicit"
		intrinsics "Off"
		omitframepointer "Off"
	filter "configurations:Release*"
		defines { "NDEBUG", "FW_RELEASE" }
		-- intrinsics "On"
		optimize "Full"
	filter "configurations:Release-Profiling"
		defines { "FW_PROFILING" }
		symbols "On" -- For tracy
	filter { "action:vs*", "configurations:Debug* or Development*" }
		symbols "Full"
	filter { "action:vs*", "platforms:Emscripten" }
		toolset "emcc"

	filter "platforms:x86"
		architecture "x86"
	filter "platforms:x64"
		architecture "x64"
	filter "platforms:Emscripten"
		architecture "x86"
		defines { "FW_PLATFORM_WEB", "FW_COMPILER_CLANG" }

	filter { "action:vs*", "configurations:*ASAN" }
		buildoptions { "/fsanitize=address" }
		editandcontinue "Off"
		flags { "NoIncrementalLink" }

	filter { "action:gmake2", "configurations:*ASAN" }
		buildoptions { "-fsanitize=address" }
		linkoptions { "-fsanitize=address" }

	filter { "system:linux", "platforms:not Emscripten" }
		toolset "clang"
		defines { "FW_OS_LINUX" }

	filter { "system:linux" }
		defines { "FW_COMPILER_CLANG" }
		buildoptions { "-Wfatal-errors" }
		disablewarnings { "macro-redefined", "switch", "nonportable-include-path" }

	filter { "system:windows", "platforms:not Emscripten" }
		defines { "FW_OS_WINDOWS" }

	filter { "action:vs*", "platforms:not Emscripten" }
		defines {
			"FW_COMPILER_MSVC",
			"NOMINMAX",
			"_CRT_SECURE_NO_WARNINGS",
			"_SILENCE_CXX20_CISO646_REMOVED_WARNING",
			"_SILENCE_CXX23_ALIGNED_STORAGE_DEPRECATION_WARNING"
		}

		staticruntime "on"
		libdirs {  "dep/lib" }
		buildoptions { "/Zc:__cplusplus" }

	filter { "platforms:Emscripten" }
		defines { "FW_PLATFORM_WEB" }
		disablewarnings { "macro-redefined", "switch" }
		buildoptions { "-matomics", "-mbulk-memory", "-msimd128" }

	filter { "platforms:not Emscripten" }
		defines { "FW_PLATFORM_STANDALONE" }

		filter { "system:macosx", "options:not emscripten" }
		defines { "FW_OS_MACOS", "FW_OS_POSIX" }

		xcodebuildsettings {
			["MACOSX_DEPLOYMENT_TARGET"] = "10.15",
			--["CODE_SIGN_IDENTITY"] = "",
			--["PROVISIONING_PROFILE_SPECIFIER"] = "",
			--["PRODUCT_BUNDLE_IDENTIFIER"] = "com.tommitytom.app.RetroPlug"
		};

		buildoptions {
			"-mmacosx-version-min=10.15"
		}

		linkoptions {
			"-mmacosx-version-min=10.15"
		}

	filter {}
end

function util.liveppCompat()
	filter { "action:vs*" }
		editAndContinue "off"
		symbols "Full"
	filter {}
end

function util.liveppCompatLink()
	premake.override(premake.vstudio.vc2010, "optimizeReferences", function(base, cfg)
		return
	end)

	util.liveppCompat()

	filter { "action:vs*" }
		linkoptions { "/FUNCTIONPADMIN", "/OPT:NOREF", "/OPT:NOICF" }
	filter {}
end

function util.disableFastUpToDateCheck(projectNames)
	require "vstudio"
	local p = premake;
	local vc = p.vstudio.vc2010;

	function disableFastUpToDateCheck(prj, cfg)
		for _, value in pairs(projectNames) do
			if prj.name == value then
				vc.element("DisableFastUpToDateCheck", nil, "true")
			end
		end
	end

	p.override(vc.elements, "globalsCondition", function(oldfn, prj, cfg)
		local elements = oldfn(prj, cfg)
		elements = table.join(elements, { disableFastUpToDateCheck })
		return elements
	end)

	filter {}
end

function util.setupPch(name, dir)
	filter { "system:windows" }
		includedirs { dir }

		pchheader(name .. ".h")
		pchsource(dir .. "/" .. name .. ".cpp")
		forceincludes { name .. ".h" }

	filter "files:**.c"
		flags { "NoPCH" }

	filter {}
end

function util.createGeneratorProject(configPaths)
	local commands = {}
	for _ ,v in ipairs(configPaths) do
		table.insert(commands, "%{wks.location}/bin/%{cfg.platform}/%{cfg.buildcfg}/ScriptCompiler " .. v ..  " %{cfg.platform}")
	end

	group "0 - Build"

	project "generator"
		kind "Utility"

		filter { "platforms:not Emscripten" }
			dependson { "ScriptCompiler" }
			prebuildcommands(commands)

		filter {}

	group ""
end

function util.createConfigureProject(includeDeps)
	local includeDepArg = ""
	if includeDeps == true then
		includeDepArg = " --include-deps"
	end

	group "0 - Build"

	project "configure"
		kind "Utility"

		dependson { "generator" }

		files { "../premake5.lua" }

		filter { "system:windows" }
			prebuildcommands {
				"cd ../..",
				"premake5" .. includeDepArg .. " " .. _ACTION
			}

		filter {}

	group ""
end

function util.interp(s, tab)
	return (s:gsub('($%b{})', function(w)
		local sub = tab[w:sub(3, -2)]
		if sub ~= nil then
			return sub
		end

		print("WARNING: Failed to replace " .. w:sub(3, -2) .. ": No matching field in supplied table")
		return w
	end))
end

return util
