local inspect = require"inspect"

local function tableFind(tab, el)
    for index, value in pairs(tab) do
        if value == el then
            return index
        end
	end
end

-- Append the contents of t2 to t1
local function tableConcat(t1, t2)
	for k,v in pairs(t2) do
		t1[k] = v
	end

	return t1
end

local function tableRemoveElement(tab, el)
	local idx = tableFind(tab, el)
	if idx ~= nil then
		table.remove(tab, idx)
	end
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

local function handleInput(map, key, down, pressed, hooks, buttonStream)
	local handled = false

	-- Do a basic map from key to button
	local found = map.lookup[key]
	if found ~= nil then
		if type(found) == "function" then
			handled = found(down)
		elseif buttonStream ~= nil then
			local skip = false
			for _, v in ipairs(hooks) do
				skip = v.fn(v.obj, found, down) == false
				if skip == true then
					break
				end
			end

			if skip == false then
				if down == true then
					buttonStream:holdDuration(found, 0)
				else
					buttonStream:releaseDuration(found, 0)
				end
			end
		end
	end

	-- If the key is being pressed look for combos
	if down == true then
		table.insert(pressed, key)
		local func = matchCombo(map.combos, pressed)
		if func ~= nil and type(func) == "function" then
			handled = func(down)
		end
	else
		tableRemoveElement(pressed, key)
	end

	return handled
end

local function inputMap(config, map)
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

return {
	tableFind = tableFind,
	tableConcat = tableConcat,
	tableRemoveElement = tableRemoveElement,
	handleInput = handleInput,
	inputMap = inputMap
}
