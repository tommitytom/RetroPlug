local util = require("util")
local inspect = require("inspect")

local MidiPassthrough = component({ name = "MIDI Passthrough" })
function MidiPassthrough:init()

end

function MidiPassthrough:onTransportChanged()
end

function MidiPassthrough:onMidi(message)
    self.system:sendSerialByte(message.offset, message.status, 8)
    self.system:sendSerialByte(message.offset, message.data1, 8)
    self.system:sendSerialByte(message.offset, message.data2, 8)
end

return MidiPassthrough
