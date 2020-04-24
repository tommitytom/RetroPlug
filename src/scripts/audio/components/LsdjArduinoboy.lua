local util = require("util")
local midi = require("midi")

local LsdjSyncModes = {
	Off = 0,
	MidiSync = 1,
	MidiSyncArduinoboy = 2,
	MidiMap = 3
}

local function midiMapRowNumber(channel, note)
	if channel == 0 then
		return note
	elseif channel == 1 then
		return note + 128
	end

	return -1;
end

local LsdjArduinoboy = component({ name = "LSDj Arduinoboy", romName = "LSDj*" })
function LsdjArduinoboy:init()
	self.syncMode = LsdjSyncModes.Off
	self.autoPlay = false
	self._playing = false
	self._lastRow = -1
	self._tempoDivisor = 1
end

function LsdjArduinoboy:onSerialize(target)
	util.serializeComponent(self, target)
end

function LsdjArduinoboy:onDeserialize(source)
	util.deserializeComponent(self, source)
end

function LsdjArduinoboy:onMenu(menu)
	return menu
		:subMenu("LSDj")
			:subMenu("Sync")
				:multiSelect({ "Off", "MIDI Sync", "MIDI Sync (Arduinoboy)", "MIDI Map"}, self.syncMode, function(idx)
					self.syncMode = idx
				end)
				:separator()
				:select("Autoplay", self.autoPlay, function(value) self.autoPlay = value end)
end

function LsdjArduinoboy:onTransportChanged(running)
	if self.autoPlay == true then
		self.system():buttons():press(Button.Start)
	end
end

function LsdjArduinoboy:onPpq(offset)
	if self.syncMode == LsdjSyncModes.MidiSync then
		self:system():sendSerialByte(offset, 0xF8)
	elseif self.syncMode == LsdjSyncModes.MidiSyncArduinoboy then
		if self._playing == true then
			self:system():sendSerialByte(offset, 0xF8)
		end
	elseif self.syncMode == LsdjSyncModes.MidiSync then
		self:system():sendSerialByte(offset, 0xFF)
	end
end

local enumtil = require("util.enum")

function LsdjArduinoboy:onMidi(msg)
	local status = msg.status
	if status == midi.Status.System then
		if msg.systemStatus == midi.SystemStatus.TimingClock then
			self:onPpq(msg.offset)
		else
			print(enumtil.toEnumString(midi.SystemStatus, msg.systemStatus))
		end
	end

	if self.syncMode == LsdjSyncModes.MidiSyncArduinoboy then
		if status == midi.Status.NoteOn then
			if 	   msg.note == 24 then self._playing = true
			elseif msg.note == 25 then self._playing = false
			elseif msg.note == 26 then self._tempoDivisor = 1
			elseif msg.note == 27 then self._tempoDivisor = 2
			elseif msg.note == 28 then self._tempoDivisor = 4
			elseif msg.note == 29 then self._tempoDivisor = 8
			elseif msg.note >= 30 then
				self:system():sendSerialByte(msg.offset, msg.note - 30)
			end
		end
	elseif self.syncMode == LsdjSyncModes.MidiMap then
		-- Notes trigger row numbers
		if status == midi.Status.NoteOn then
			local rowIdx = midiMapRowNumber(msg.channel, msg.note)
			if rowIdx ~= -1 then
				self:system():sendSerialByte(msg.offset, rowIdx)
				self._lastRow = rowIdx
			end
		elseif status == midi.Status.Noteff then
			local rowIdx = midiMapRowNumber(msg.channel, msg.note)
			if rowIdx == self._lastRow then
				self:system():sendSerialByte(msg.offset, 0xFE)
				self._lastRow = -1
			end
		elseif msg.type == "stop" then
			self:system():sendSerialByte(msg.offset, 0xFE)
		end
	end
end

return LsdjArduinoboy
