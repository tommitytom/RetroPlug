require("constants")
local inspect = require 'inspect'

Active = nil

local _config = { system = "gameboy" }
local _maps = {
	key = { all = {}, valid = {}, combo = {}, comboActive = false },
	pad = { all = {}, valid = {}, combo = {}, comboActive = false },
	midi = { all = {}, valid = {}, combo = {}, comboActive = false }
}

local function tableConcat(t1, t2)
	for k,v in pairs(t2) do
		t1[k] = v
	end

    return t1
end

local function findConfigMap(root, config)
	for i,v in ipairs(root) do
		if v.system == config.system then
			if v.romName == nil then
				return v.map
			end

			if v.romName == config.romName then
				return v.map
			elseif config.romName ~= nil and v.romName:find(config.romName) ~= nil then
				return v.map
			end
		end
	end
end

local function findOrCreateConfig(root, config)
	local found
	for _, v in ipairs(root) do
		if v.system == config.system and v.romName == config.romName then
			found = v
			break
		end
	end

	if found == nil then
		found = { system = config.system, romName = config.romName, map = {} }
		table.insert(root, found)
	end

	return found
end

local function mapSetup(root, config, map)
	if map == nil then
		map = config
		config = { system = "gameboy" }
	end

	local found = findOrCreateConfig(root.all, config)
	tableConcat(found.map, map)
end

local function matchCombo(root, combo)
	if #combo == 1 then
		return root[combo[1]]
	end

	for k, v in pairs(root) do
		if type(k) == "table" and #k == #combo then
			-- Make sure the combo matches in the correct order
			local match = true
			for i = 1, #combo, 1 do
				if k[i] ~= combo[i] then
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

local function tablefind(tab, el)
    for index, value in pairs(tab) do
        if value == el then
            return index
        end
    end
end

local function onInput(root, element, down)
	if down == true then
		if #root.combo == 0 then
			root.comboActive = true
		end

		if root.comboActive == true then
			table.insert(root.combo, element)
		end

		local found = matchCombo(root.valid, root.combo)
		print(found)
		if found ~= nil then
			if type(found) == "number" then
				print"button"
				Active:setButtonState(found, true)
			elseif type(found) == "function" then
				found(element, down)
			end
		end
	else
		local found = root.valid[element]
		if found ~= nil and type(found) == "number" then
			Active:setButtonState(found, false)
		end

		local idx = tablefind(root.combo, element)
		if idx ~= nil then table.remove(root.combo, idx) end

		root.comboActive = false
	end
end

function _onKey(key, down)
	onInput(_maps.key, key.vk, down)
end

function _onPad(button, down)
	onInput(_maps.pad, button, down)
end

function _onMidi(note, down)
	onInput(_maps.midi, note, down)
end

function _setup(systemType, romName)
	_config = { system = systemType, romName = romName }
	for _, v in pairs(_maps) do
		v.valid = {}
		local found = findConfigMap(v.all, _config)
		if found ~= nil then
			tableConcat(v.valid, found)
		end
	end
end

function keyMap(config, map)
	mapSetup(_maps.key, config, map)
end

function padMap(config, map)
	mapSetup(_maps.pad, config, map)
end

function midiMap(config, map)
	mapSetup(_maps.midi, config, map)
end
