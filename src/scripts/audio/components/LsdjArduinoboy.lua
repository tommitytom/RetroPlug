local util = require("util")
local inspect = require("inspect")

local LsdjArduinoboy = component({ name = "LSDJ Arduinoboy", romName = "LSDj*" })
function LsdjArduinoboy:init()

end

function LsdjArduinoboy:onTransportChanged(running)
end

function LsdjArduinoboy:onMidi(message)
	self.system:sendMidi(message)
end

return LsdjArduinoboy
