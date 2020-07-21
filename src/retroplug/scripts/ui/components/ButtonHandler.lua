--local util = require("util")
local inpututil = require("util.input")

local _mapLookup = {

}

local _maps = {
	key = {},
	pad = {},
	midi = {}
}

local _parsing = nil

function _beginInputParser(name)
	_parsing = {
		name = name,
		config = {},
		key = {},
		pad = {},
		midi = {}
	}
end

function InputConfig(config)
	_parsing.config = config
end

function KeyMap(config, map)
	table.insert(_parsing.key, inpututil.inputMap(config, map))
end

function PadMap(config, map)
	table.insert(_parsing.pad, inpututil.inputMap(config, map))
end

function MidiMap(config, map)
	--inpututil.inputMap(_maps.midi, config, map)
end

function _endInputParser()
	assert(_parsing ~= nil)
	_mapLookup[_parsing.name] = _parsing
	_maps = _parsing
	_parsing = nil
end

local ButtonHandler = component({ name = "Button Handler", system = true })
function ButtonHandler:init()
	self._keysPressed = {}
	self._padbuttonsPressed = {}
	self._keyMap = { lookup = {}, combos = {} }
	self._padMap = { lookup = {}, combos = {} }
	self._actions = {}
	self._buttonHooks = {}
end

function ButtonHandler:onRomLoad()
	self:_updateMaps(self:system().desc.romName)
end

function ButtonHandler:onReload()
	self:_updateMaps(self:system().desc.romName)
end

function ButtonHandler:_updateMaps(romName)
	self._keyMap = { lookup = {}, combos = {} }
	self._padMap = { lookup = {}, combos = {} }
	inpututil.mergeInputMaps(_maps.key, self._keyMap, self._actions, romName)
	inpututil.mergeInputMaps(_maps.pad, self._padMap, self._actions, romName)
end

function ButtonHandler:onComponentsInitialized(components)
	self._actions = {}
	self._buttonHooks = {}

	for _, v in ipairs(components) do
		self._actions[string.lower(v.__desc.name)] = v.__actions
		if v.onBeforeButton ~= nil then
			table.insert(self._buttonHooks, {
				obj = v,
				fn = v.onBeforeButton
			})
		end
	end
end

function ButtonHandler:onKey(key, down)
	return inpututil.handleInput(self._keyMap, key, down, self._keysPressed, self._buttonHooks, self:system():buttons())
end

function ButtonHandler:onPadButton(button, down)
	return inpututil.handleInput(self._padMap, button, down, self._padbuttonsPressed, self._buttonHooks, self:system():buttons())
end

return ButtonHandler
