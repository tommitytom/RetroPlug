local util = require("util")
local inspect = require("inspect")

local LsdjArduinoboy = component({ name = "LSDJ Arduinoboy", romName = "LSDj*" })
function LsdjArduinoboy:init()

end

local function syncHandler(idx)
end

function LsdjArduinoboy:onMenu()
	return {
		["LSDj"] = {
			["Sync"] = MultiSelect {
				items = {
					"MIDI Sync",
					"MIDI Sync (Arduinoboy)",
					"MIDI Map",
					"-",
					"Autoplay"
				},
				handler = syncHandler
			}
		}
	}
end

function LsdjArduinoboy:onTransportChanged(running)
end

function LsdjArduinoboy:onMidi(message)
	self.system:sendMidi(message)
end

return LsdjArduinoboy
