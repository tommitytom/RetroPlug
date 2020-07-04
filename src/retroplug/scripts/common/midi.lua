local MidiStatus = {
	None = 0,
	NoteOff = 8,
	NoteOn = 9,
	PolyAftertouch = 10,
	ControlChange = 11,
	ProgramChange = 12,
	ChannelAftertouch = 13,
	PitchWheel = 14,
	System = 15
}

local MidiSystemStatus = {
	-- System common
	SysEx = 0,
	TimeCode = 1,
	SongPosition = 2,
	SongSelect = 3,
	TuneRequest = 6,
	EndSysEx = 7,

	-- System real-time
	TimingClock = 8,
	SequenceStart = 10,
	SequenceContinue = 11,
	SequenceStop = 12,
	ActiveSensing = 14,
	Reset = 15
}

local midiMsgProperties = {
	note = function(c) return c.data1 end,
	status = function(c) return c.statusByte >> 4 end,
	channel = function(c) return c.statusByte & 0x0F end,
	systemStatus = function(c) return c.statusByte & 0x0F end,
}

local midiMessageMeta = {
	__index = function(c, k)
		local f = midiMsgProperties[k]
		if f ~= nil then return f(c) end
		return rawget(c, k)
	end
}

local function MidiMessage(offset, status, data1, data2)
	local msg = { offset = offset, statusByte = status, data1 = data1, data2 = data2 }
	setmetatable(msg, midiMessageMeta)
	return msg
end

return {
	Status = MidiStatus,
	SystemStatus = MidiSystemStatus,
	Message = MidiMessage
}
