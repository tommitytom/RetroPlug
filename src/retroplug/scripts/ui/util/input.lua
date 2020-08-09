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

function module.handleInput(mapGroup, key, down, pressed, system)
	local handled = false
	for _, map in ipairs(mapGroup) do
		-- Do a basic map from key to button
		local found = map.lookup[key]
		if found ~= nil then
			if type(found) == "table" then
				if found.func ~= nil then
					if found.func(down, system) ~= false then handled = true end
				end
			elseif system ~= nil then
				local buttons = system:buttons()
				if buttons ~= nil then
					if down == true then
						buttons:holdDuration(found, 0)
					else
						buttons:releaseDuration(found, 0)
					end
				end

				handled = true
			end
		end

		-- If the key is being pressed look for combos
		if down == true then
			local func = matchCombo(map.combos, pressed)
			if func ~= nil and type(func) == "function" then
				if func(down) ~= false then
					handled = true
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

return module
