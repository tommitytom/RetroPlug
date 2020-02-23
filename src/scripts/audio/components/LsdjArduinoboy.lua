local util = require("util")
local inspect = require("inspect")
local m = require("Menu")

local LsdjSyncModes = {
	Off = 0,
	MidiSync = 1,
	MidiSyncArduinoboy = 2,
	MidiMap = 3
}

local LsdjArduinoboy = component({ name = "LSDj Arduinoboy", romName = "LSDj*" })
function LsdjArduinoboy:init()
	self.syncMode = LsdjSyncModes.Off
	self.autoPlay = false
end

function LsdjArduinoboy:onMenu(menu)
	return menu
		:subMenu("LSDj")
			:subMenu("Sync")
				:multiSelect({
					"Off",
					"MIDI Sync",
					"MIDI Sync (Arduinoboy)",
					"MIDI Map",
				}, self.syncMode, function(idx) self.syncMode = idx end)
				:separator()
				:select("Autoplay", self.autoPlay, function(value) print(value); self.autoPlay = value end)
end

function LsdjArduinoboy:onTransportChanged(running)
	print("transport changed", running)
	if self.autoPlay == true then
		self.buttons:press(Button.Start)
	end
end

function LsdjArduinoboy:onMidi(message)
	--self.system:sendMidi(message)
end

return LsdjArduinoboy
