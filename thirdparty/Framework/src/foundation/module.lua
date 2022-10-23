local m = {
	name = "Foundation",
	author = "tommitytom <hello@tommitytom.co.uk>",
	dependencies = {
		"bgfx", "spdlog"
	}
}

function m.include()
	sysincludedirs {
		"thirdparty"
	}

	includedirs {
		"src",
		"generated",
		"resources"
	}
end

function m.source()
	files {
		"src/foundation/**.h",
		"src/foundation/**.cpp"
	}
end

function m.link()
	links { "Foundation" }
end

return m