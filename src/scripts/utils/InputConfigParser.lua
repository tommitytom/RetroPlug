local log = require("log")
local inspect = require("inspect")

Action = {}
setmetatable(Action, {
	__index = function(table, componentName)
		local actionNameTable = {}
		setmetatable(actionNameTable, {
			__index = function(table, actionName)
				return {
					component = componentName,
					action = actionName
				}
			end
		})

		return actionNameTable
	end
})

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

local function tableEmpty(tab)
	for _, _ in pairs(tab) do
		return false
	end

	return true
end

parsed = {
	config = {},
	key = { system = {}, global = {} },
	pad = { system = {}, global = {} }
}

--Button = Button,
--Key = Key,
--Pad = Pad,
--HostType = HostType,

InputConfig = function(config) parsed.config = config or {} end
KeyMap = function(config, map) handleMapInput(parsed.key.system, config, map) end
PadMap = function(config, map) handleMapInput(parsed.pad.system, config, map) end
GlobalKeyMap = function(config, map) handleMapInput(parsed.key.global, config, map) end
GlobalPadMap = function(config, map) handleMapInput(parsed.pad.global, config, map) end

function cleanData()
	if parsed.config.name == nil then parsed.config.name = parsed.config.filename end

	if tableEmpty(parsed.key.system) and tableEmpty(parsed.key.global) then
		parsed.key = nil
	else
		parsed.key.filename = parsed.config.filename
	end

	if tableEmpty(parsed.pad.system) and tableEmpty(parsed.pad.global) then
		parsed.pad = nil
	else
		parsed.pad.filename = parsed.config.filename
	end

	log.info(inspect(parsed))
end

local function matchCombo(combos, pressed)
	for combo, v in pairs(combos) do
		if #combo == #pressed then
			local match = true
			for i = 1, #combo, 1 do
				if combo[i] ~= pressed[i] then
					match = false
					break
				end
			end

			if match == true then
				return v
			end
		end
	end
end

local function handleInput(mapGroup, key, down, pressed, hooks, system)
	local handled = false
	for _, map in ipairs(mapGroup) do
		-- Do a basic map from key to button
		local found = map.lookup[key]
		if found ~= nil then
			if type(found) == "table" then
				if found.func ~= nil then
					if found.func(down, system) ~= false then
						handled = true
					end
				end
			elseif system ~= nil then
				for _, fn in ipairs(hooks) do
					if fn(found, down) ~= false then
						handled = true
					end
				end

				if handled == false then
					system:setButtonState(found, down)
					handled = true
				end
			end
		end

		-- If the key is being pressed look for combos
		if down == true then
			local found = matchCombo(map.combos, pressed)
			if found ~= nil and type(found) == "table" then
				if found.func ~= nil then
					if found.func(down, system) ~= false then
						handled = true
					end
				end
			end
		else
			-- TODO: Check for combos that have been released?
		end
	end

	return handled
end

local keysPressed = {}
local buttonsPressed = {}
local buttonHooks = {}

local function tableFind(tab, el)
    for index, value in pairs(tab) do
        if value == el then
            return index
        end
	end
end

local function tableRemoveElement(tab, el)
	local idx = tableFind(tab, el)
	if idx ~= nil then
		table.remove(tab, idx)
	end
end

function processKey(key, down, system)
	if down == true then
		table.insert(keysPressed, key)
	else
		tableRemoveElement(keysPressed, key)
	end

	local handled = handleInput(parsed.key.global, key, down, keysPressed, buttonHooks)

	if handled ~= true and system ~= nil then
		handled = handleInput(parsed.key.system, key, down, keysPressed, buttonHooks, system)
	end

	return handled
end

--[[
local InputConfig = {}
function InputConfig.init()
	self.configs = {}
end

function InputConfig.loadFromString(name, code)
	local env = createEnv()
	local f, err = load(code, name, "t", env.env)

	self:parseConfig(f, err, env.parsed, name, name)
end

function InputConfig.load(path)
	path = pathutil.clean(path)

	local filename = pathutil.filename(path)
	local env = createEnv()
	local f, err = loadfile(path, "t", env.env)

	self:parseConfig(f, err, env.parsed, filename, path)
end

function InputConfig.parseConfig(f, err, parsed, filename, path)
	if f ~= nil then
		local ok, ret = pcall(f)

		if ok then
			parsed.config.path = path
			parsed.config.filename = filename

			cleanData(parsed)
			table.insert(self.configs, parsed)
		else
			error("Error in button config: ", ret)
		end
	else
		log.error(err)
		error("Failed to load " .. filename)
	end
end

return InputConfig
]]