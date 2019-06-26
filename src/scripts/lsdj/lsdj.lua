return {
	romMatch = function(name) startsWith(name:lower(), "lsdj") end,
	groups = {
		{
			"Off",
			require("MidiSync"),
			require("MidiSyncArduinoboy"),
			require("MidiMap")		
		}, {
			require("AutoPlay")
		}
	}
}
