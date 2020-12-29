local util = require("util")

local module = {}

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

function module.handleInput(mapGroup, key, down, pressed, hooks, system)
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

				local buttons = system:buttons()
				if handled == false and buttons ~= nil then
					if down == true then
						buttons:holdDuration(found, 0)
					else
						buttons:releaseDuration(found, 0)
					end

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

function module.inputMap(config, map)
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
		end
	end

	return { config = config, lookup = lookup, combos = combos }
end

local function concatTarget(target, map, actions)
	for k, v in pairs(map) do
		if type(v) == "number" then
			target[k] = v
		elseif type(v) == "table" then
			local c = actions[string.lower(v.component)]
			if c ~= nil then
				local f = c[string.lower(v.action)]
				if f ~= nil then
					target[k] = f
				else
					print("Failed to find Action." .. v.component .. "." .. v.action)
				end
			else
				print("Failed to find Action." .. v.component .. "." .. v.action)
			end
		end
	end
end

function module.mergeInputMaps(source, target, actions, romName)
	for _, map in ipairs(source) do
		local merge = false
		if map.config.romName == nil then
			merge = true
		elseif romName:find(map.config.romName) then
			merge = true
		end

		if merge == true then
			concatTarget(target.lookup, map.lookup, actions)
			concatTarget(target.combos, map.combos, actions)
		end
	end
end

local function findInputConfig(configs, filename)
	for i, v in ipairs(configs) do
		if v.config.filename == filename then
			return v
		end
	end

	return nil
end

function module.getInputMap(inputConfigs, input)
	print(input.key, input.pad)
	local keyMap = findInputConfig(inputConfigs, input.key)
	local padMap = findInputConfig(inputConfigs, input.pad)

	if keyMap == nil then
		keyMap = findInputConfig(inputConfigs, "default.lua")
		assert(keyMap)
	end

	if padMap == nil then
		padMap = findInputConfig(inputConfigs, "default.lua")
		assert(padMap)
	end

	return {
		key = keyMap.key,
		pad = padMap.pad
	}
end

return module
