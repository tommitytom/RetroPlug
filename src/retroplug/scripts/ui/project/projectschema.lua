local s = require("schema")

local schema = {}

schema["1.0.0"] = s.Record {
	path = s.Optional(s.String),
	projectVersion = s.OneOf("1.0.0"),
	retroPlugVersion = s.String,
	systems = s.Collection(s.Record {
		systemType = s.OneOf("sameBoy"),
		romPath = s.String,
		sramPath = s.String,
		sameBoy = s.Record {
			model = s.OneOf("auto", "agb", "cgbC", "cgbE", "dmgB"),
			gameLink = s.Boolean,
			skipBootRom = s.Boolean
		},
		input = s.Record {
			key = s.String,
			pad = s.String,
		},
		uiComponents = s.Map(s.String, s.Any),
		audioComponents = s.Map(s.String, s.Any),
		includeRom = s.Boolean,
		_stateData = s.Optional(s.Any)
	}),
	settings = s.Record {
		saveType = s.OneOf("sram", "state"),
		audioRouting = s.OneOf("stereoMixDown", "twoChannelsPerChannel", "twoChannelsPerInstance"),
		zoom = s.NumberFrom(0, 4),
		midiRouting = s.OneOf("oneChannelPerInstance", "fourChannelsPerInstance", "sendToAll"),
		layout = s.OneOf("auto", "column", "grid", "row")
	}
}

return schema
