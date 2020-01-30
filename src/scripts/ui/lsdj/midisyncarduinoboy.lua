--[[local MidiSyncArduinoboy = plugin({ name = "MIDI Sync (Arduinoboy Variation)" })

function MidiSyncArduinoboy:create(system)
	self.playing = false
	self.tempoDivisor = 1
end

function MidiSyncArduinoboy:onMidi(system, msg)
	if msg.status == "noteOn" then
		if 		msg.note == 24 then self.playing = true
		else if msg.note == 25 then self.playing = false
		else if msg.note == 26 then self.tempoDivisor = 1
		else if msg.note == 27 then self.tempoDivisor = 2
		else if msg.note == 28 then self.tempoDivisor = 4
		else if msg.note == 29 then self.tempoDivisor = 8
		else if msg.note >= 30 then
			system:sendSerialByte(msg.note - 30)
		end
	elseif msg.type == "clock" and self.playing == true then
		system:sendSerialByte(0xF8)
	end
end

return MidiSyncArduinoboy]]