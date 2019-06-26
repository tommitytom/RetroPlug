local MidiSync = plugin({ name = "MIDI Sync" })

function MidiSync:onMidi(system, msg)
	if msg.status == "clock" then
		system:sendSerialByte(0xF8)
	end
end

return MidiSync

return plugin({
	name = "MIDI Sync"
}, function(plug) 

	function plug:onMidi(system, msg)
		if msg.status == "clock" then
			system:sendSerialByte(0xF8)
		end
	end	

end)