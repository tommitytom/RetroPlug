local MidiPassthrough = component({ name = "MIDI Passthrough", romName = "MGB" })
function MidiPassthrough.init()

end

function MidiPassthrough.onTransportChanged()
end

function MidiPassthrough.requires()
	return System ~= nil and System.desc.romName:match("MGB") ~= nil
end

function MidiPassthrough.onMidi(system, message)
	system:sendSerialByte(message.offset, message.statusByte, 8)
	system:sendSerialByte(message.offset, message.data1, 8)
	system:sendSerialByte(message.offset, message.data2, 8)
end

return MidiPassthrough
