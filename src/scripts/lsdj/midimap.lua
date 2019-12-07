local MidiMap = plugin({ name = "MIDI Map" })

local function midiMapRowNumber(channel, note)
	if channel == 0 then
		return note
	elseif channel == 1 then
		return note + 128
	end

	return -1;
end

function MidiMap:create(system)
	self.lastRow = -1
end

function MidiMap:onMidi(system, msg)
	if msg.status == "noteOn" then
		local rowIdx = midiMapRowNumber(msg.channel, msg.note)
		if rowIdx ~= -1 then
			system:sendSerialByte(msg.offset, rowIdx)
			self.lastRow = rowIdx
		end
	else if msg.status == "noteOff" then
		local rowIdx = midiMapRowNumber(msg.channel, msg.note)
		if rowIdx == self.lastRow then
			system:sendSerialByte(msg.mOffset, 0xFE)
			self.lastRow = -1
		end
	elseif msg.type == "stop" then
		system:sendSerialByte(0xFE)
	end
end

return MidiMap
