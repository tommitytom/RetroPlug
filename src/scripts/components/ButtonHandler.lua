local util = require("../util")

local _maps = {
	key = {},
	pad = {},
	midi = {}
}

local function mergeInputMaps(source, target, romName)
	for _, map in ipairs(source) do
		local merge = false
		if map.config.romName == nil then
			merge = true
		elseif romName:find(map.config.romName) then
			merge = true
		end

		if merge == true then
			util.tableConcat(target.lookup, map.lookup)
			util.tableConcat(target.combos, map.combos)
		end
	end
end

function KeyMap(config, map)
	table.insert(_maps.key, util.inputMap(config, map))
end

function PadMap(config, map)
	table.insert(_maps.pad, util.inputMap(config, map))
end

function MidiMap(config, map)
	--util.inputMap(_maps.midi, config, map)
end

local ButtonHandler = component({ name = "Button Handler" })
function ButtonHandler:init()
	self._keysPressed = {}
	self._padbuttonsPressed = {}
	self._keyMap = { lookup = {}, combos = {} }
	self._padMap = { lookup = {}, combos = {} }
end

function ButtonHandler:onRomLoaded(romName, romPath)
	self._keyMap = { lookup = {}, combos = {} }
	self._padMap = { lookup = {}, combos = {} }
	mergeInputMaps(_maps.key, self._keyMap, romName)
	mergeInputMaps(_maps.pad, self._padMap, romName)
end

function ButtonHandler:onKey(key, down)
	util.handleInput(self._keyMap, key, down, self._keysPressed, self.system)
end

function ButtonHandler:onPadButton(button, down)
	util.handleInput(self._padMap, button, down, self._padbuttonsPressed, self.system)
end

return ButtonHandler
