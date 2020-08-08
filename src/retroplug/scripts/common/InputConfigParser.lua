local class = require("class")
local Action = require("Action")
local log = require("log")
local pathutil = require("pathutil")

local InputConfig = class()
function InputConfig:init()
	self._configs = {}
end

local function handleMapInput(target, config, map)
	if map == nil then
		map = config
		config = {}
	end

	table.insert(target, { config = config, map = map })
end

function InputConfig:load(path)
	local parsed = {
		config = {},
		key = { system = {}, global = {} },
		pad = { system = {}, global = {} }
	}

	local env = {
		Action = Action,

		Button = Button,
		Key = Key,
		Pad = Pad,

		InputConfig = function(config) parsed.config = config or {} end,
		KeyMap = function(config, map) handleMapInput(parsed.key.system, config, map) end,
		PadMap = function(config, map) handleMapInput(parsed.pad.system, config, map) end,
		GlobalKeyMap = function(config, map) handleMapInput(parsed.key.global, config, map) end,
		GlobalPadMap = function(config, map) handleMapInput(parsed.pad.global, config, map) end
	}

	local f = loadfile(path, "t", env)
	if f ~= nil then
		local ok, ret = pcall(f)
		if ok then
			parsed.config.path = pathutil.clean(path)
			table.insert(self._configs, parsed)
			--log.obj(parsed)
		else
			print("Error in button config: ", ret)
		end
	else
		print("Failed to load", path)
	end
end

return InputConfig
