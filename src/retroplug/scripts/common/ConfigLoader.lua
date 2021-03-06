local s = require("schema")
local serpent = require("serpent")
local log = require("log")
local util = require("util")

local function loadText(path)
	local file = io.open(path, "rb") -- r read mode and b binary mode
    if not file then return nil end
    local content = file:read("*a") -- *a or *all reads the whole file
    file:close()
    return content
end

local module = {}

local configSchema = s.Record {
	system = s.Record {
		uiComponents = s.Record {},
		audioComponents = s.Record {},
		sameBoy = s.Record {
			model = s.OneOf("auto", "agb", "cgbC", "cgbE", "dmgB"),
			gameLink = s.Boolean,
			skipBootRom = s.Boolean
		},
		input = s.Record {
			key = s.String,
			pad = s.String,
		}
	},
	project = s.Record {
		saveType = s.OneOf("sram", "state"),
		audioRouting = s.OneOf("stereoMixDown", "twoChannelsPerChannel", "twoChannelsPerInstance"),
		zoom = s.NumberFrom(0, 4),
		midiRouting = s.OneOf("oneChannelPerInstance", "fourChannelsPerInstance", "sendToAll"),
		layout = s.OneOf("auto", "column", "grid", "row"),
		includeRom = s.Boolean
	}
}

function module.loadConfigFromString(code)
	local ok, config = serpent.load(code, { safe = true })
	if ok then
		local valErr = s.CheckSchema(config, configSchema)
		if valErr then
			log.error(s.FormatOutput(valErr))
			return false
		end

		config.system.sameBoy.model = util.fromEnumString(GameboyModel, config.system.sameBoy.model)

		return true, config
	end

	return false
end

function module.loadConfigFromPath(path)
	log.info("Loading config from " .. path)
	
	local code = loadText(path)
	if code then
		return module.loadConfigFromString(code)
	end
end

return module
