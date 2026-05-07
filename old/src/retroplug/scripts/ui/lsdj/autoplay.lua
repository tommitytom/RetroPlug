local AutoPlay = plugin({ name = "Auto Play" })

function AutoPlay:onMidi(system, msg)
	if msg.type == "start" or msg.type == "stop" then
		system.buttons:press(GameboyButton.Start)
	end	
end

return AutoPlay
