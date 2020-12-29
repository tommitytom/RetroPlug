local class = require("class")
local Action = require("Action")
local log = require("log")
local pathutil = require("pathutil")

-- Processes the data passed in by the user
local function handleMapInput(target, config, map)
	if map == nil then
		map = config
		config = {}
	end

	local lookup = {}
	local combos = {}

	for k, v in pairs(map) do
		if type(k) == "number" then
			lookup[k] = v
		elseif type(k) == "table" then
			combos[k] = v
		else
			log.error("Failed to load input map: string fields are not supported")
		end
	end

	table.insert(target, { config = config, lookup = lookup, combos = combos })
end

local InputConfig = class()
function InputConfig:init()
	self.configs = {}
end

local function tableEmpty(tab)
	for k, v in pairs(tab) do
		return false
	end

	return true
end

local function cleanData(data)
	if data.config.name == nil then data.config.name = data.config.filename end

	if tableEmpty(data.key.system) and tableEmpty(data.key.global) then
		data.key = nil
	else
		data.key.filename = data.config.filename
	end

	if tableEmpty(data.pad.system) and tableEmpty(data.pad.global) then
		data.pad = nil
	else
		data.pad.filename = data.config.filename
	end
end

function InputConfig:load(path)
	path = pathutil.clean(path)

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
		HostType = HostType,

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
			parsed.config.path = path
			parsed.config.filename = pathutil.filename(path)

			cleanData(parsed)
			table.insert(self.configs, parsed)
		else
			print("Error in button config: ", ret)
		end
	else
		print("Failed to load", path)
	end
end

return InputConfig
