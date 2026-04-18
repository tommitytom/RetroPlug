local RTMIDI_DIR = "thirdparty/rtmidi"

local m = {}

function m.include()
	includedirs { RTMIDI_DIR }
end

function m.source()
	m.include()

	files {
		RTMIDI_DIR .. "/RtMidi.h",
		RTMIDI_DIR .. "/RtMidi.cpp"
	}

	filter "system:linux"
		defines {
			"__LINUX_ALSA__",
			"__UNIX_JACK__"
		}

		disablewarnings { "deprecated-declarations" }
	filter {}
end

function m.link()
	m.include()
	links { "rtmidi" }
	filter "system:linux"
		links {
			"asound",
			"jack",
			"pthread"
		}
	filter {}
end

function m.project()
	project "rtmidi"
		kind "StaticLib"

		m.source()
end

return m