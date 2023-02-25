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

function util.liveppCompat()
	filter { "action:vs*" }
		editAndContinue "off"
		linkoptions { "/FUNCTIONPADMIN", "/OPT:NOREF", "/OPT:NOICF" }
		symbols "Full"
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
