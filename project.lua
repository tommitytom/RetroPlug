local m = {}

function m.include()

end

function m.source()

end

function m.link()

end

local function examples(...)
	for i, v in ipairs(arg) do

	end
end

local function createProject(path)
	local mod = dofile(path .. "/module.lua")

	project(mod.name)
	dependson { "configure" }
	kind "StaticLib"

	if mod.include then mod.include() end

	filter {}

	if mod.source then mod.source() end

	filter {}
end

local function projects(...)
	for _, v in ipairs(arg) do
		createProject(v)
	end
end

local function dependencies(...)
	for _, v in ipairs(arg) do
		if _OPTIONS["deps"] or _OPTIONS["include-deps"] then
			createProject(v)
		else
			local mod = dofile(v .. "/module.lua")
			mod.link()
		end
	end
end

local function linkDeps()

end

local function application(name)
	project(name)
	kind "Application"

end

workspace "Framework"
	dependencies {
		"thirdparty/Framework"
	}

	projects {
		"src/Application",
		"src/Audio",
		"src/Engine",
		"src/Foundation",
		"src/Graphics",
		"src/Ui"
	}

	application "RetroPlug"

	examples {
		"BasicScene",
		"Granular"
	}
