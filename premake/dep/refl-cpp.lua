local REFLCPP_DIR = "thirdparty/reflect-cpp"

local m = {}

function m.include()
	includedirs {
		REFLCPP_DIR .. "/include",
		REFLCPP_DIR .. "/include/rfl/thirdparty"
	}
end

function m.source()
	m.include()

	files {
		REFLCPP_DIR .. "/src/reflectcpp.cpp",
		REFLCPP_DIR .. "/src/reflectcpp_json.cpp",
		REFLCPP_DIR .. "/src/yyjson.c"
	}
end

function m.link()
	m.include()
	links { "refl-cpp" }
end

function m.project()
	project "refl-cpp"
		kind "StaticLib"

		m.source()
end

return m