local midi = require("midi")
local enumtil = require("util.enum")

local LsdjSyncModes = {
	Off = 0,
	MidiSync = 1,
	MidiSyncArduinoboy = 2,
	MidiMap = 3
}

local function midiMapRowNumber(channel, note)
	if channel == 0 then return note end
	if channel == 1 then return note + 128 end
	return -1;
end

local LsdjArduinoboy = component({
	name = "Arduinoboy",
	romName = "LSDj*",
	systemState = {
		syncMode = LsdjSyncModes.Off,
		autoPlay = false,
		playing = false,
		lastRow = -1,
		tempoDivisor = 1
	}
})

local function isLsdj(system)
	return system.desc.romName:match("LSDj*") ~= nil
end

function LsdjArduinoboy.requires()
	return System ~= nil and isLsdj(System)
end

function LsdjArduinoboy.onMenu(menu)
	local state = System.state.arduinoboy
	return menu
		:subMenu("LSDj")
			:subMenu("Sync")
				:multiSelect({ "Off", "MIDI Sync [MIDI]", "MIDI Sync (Arduinoboy) [MIDI]", "MIDI Map [MI. MAP]"}, state.syncMode, function(idx)
					state.syncMode = idx
				end)
				:separator()
				:select("Autoplay", state.autoPlay, function(value) state.autoPlay = value end)
end

function LsdjArduinoboy.onTransportChanged(running)
	local state = System.state.arduinoboy
	if state.autoPlay == true then
		System:buttons():press(Button.Start)
	end
end

local function onPpq(state, offset)
	print("ppq")
	if state.syncMode == LsdjSyncModes.MidiSync then
		System:sendSerialByte(offset, 0xF8)
	elseif state.syncMode == LsdjSyncModes.MidiSyncArduinoboy then
		if state.playing == true then
			System:sendSerialByte(offset, 0xF8)
		end
	elseif state.syncMode == LsdjSyncModes.MidiSync then
		System:sendSerialByte(offset, 0xFF)
	end
end

function LsdjArduinoboy.onMidi(system, msg)
	local state = system.state.arduinoboy

	local status = msg.status
	if status == midi.Status.System then
		if msg.systemStatus == midi.SystemStatus.TimingClock then
			onPpq(state, msg.offset)
		else
			print(enumtil.toEnumString(midi.SystemStatus, msg.systemStatus))
		end
	end

	if state.syncMode == LsdjSyncModes.MidiSyncArduinoboy then
		if status == midi.Status.NoteOn then
			if 	   msg.note == 24 then state.playing = true
			elseif msg.note == 25 then state.playing = false
			elseif msg.note == 26 then state.tempoDivisor = 1
			elseif msg.note == 27 then state.tempoDivisor = 2
			elseif msg.note == 28 then state.tempoDivisor = 4
			elseif msg.note == 29 then state.tempoDivisor = 8
			elseif msg.note >= 30 then
				System:sendSerialByte(msg.offset, msg.note - 30)
			end
		end
	elseif state.syncMode == LsdjSyncModes.MidiMap then
		-- Notes trigger row numbers
		if status == midi.Status.NoteOn then
			local rowIdx = midiMapRowNumber(msg.channel, msg.note)
			if rowIdx ~= -1 then
				System:sendSerialByte(msg.offset, rowIdx)
				state.lastRow = rowIdx
			end
		elseif status == midi.Status.Noteff then
			local rowIdx = midiMapRowNumber(msg.channel, msg.note)
			if rowIdx == state.lastRow then
				System:sendSerialByte(msg.offset, 0xFE)
				state.lastRow = -1
			end
		elseif msg.type == "stop" then
			System:sendSerialByte(msg.offset, 0xFE)
		end
	end
end

return LsdjArduinoboy
