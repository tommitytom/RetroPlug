local SERIAL_DIR = "thirdparty/serial"

local m = {}

function m.include()
	includedirs { SERIAL_DIR .. "/include" }
end

function m.source()
	m.include()

	files {
		SERIAL_DIR .. "/src/*.cc"
	}

	filter "system:windows"
		disablewarnings { 4101, 4244 }
		files {
			SERIAL_DIR .. "/src/impl/**win.cc"
		}

	filter {}
end

function m.link()
	m.include()
	links { "serial" }

	filter "system:windows"
		links { "setupapi" }

	filter {}
end

function m.project()
	project "serial"
		kind "StaticLib"

		m.source()
end

return m