local GlobalButtonHandler = component({ name = "Global Button Handler", global = true })

local inpututil = require("util.input")
local log = require("log")

local _maps = {
	key = {},
	pad = {},
	midi = {}
}

function GlobalButtonHandler:init()
	self.keysPressed = {}
	self.padButtonsPressed = {}
	self._keyMap = { lookup = {}, combos = {} }
	self._padMap = { lookup = {}, combos = {} }
	self._actions = {}
	self._buttonHooks = {}
end

function GlobalButtonHandler:onSetup()
	self:_updateMaps()
end

function GlobalButtonHandler:onComponentsInitialized(components)
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

function GlobalButtonHandler:_updateMaps()
	self._keyMap = { lookup = {}, combos = {} }
	self._padMap = { lookup = {}, combos = {} }

	inpututil.mergeInputMaps(_maps.key, self._keyMap, self._actions)
	inpututil.mergeInputMaps(_maps.pad, self._padMap, self._actions)
end

function GlobalButtonHandler:onKey(key, down)
	return inpututil.handleInput(self._keyMap, key, down, self.keysPressed)
end

function GlobalButtonHandler:onPadButton(button, down)
	return inpututil.handleInput(self._padMap, button, down, self.padButtonsPressed)
end

function GlobalKeyMap(config, map)
	table.insert(_maps.key, inpututil.inputMap(config, map))
end

function GlobalPadMap(config, map)
	table.insert(_maps.pad, inpututil.inputMap(config, map))
end

return GlobalButtonHandler
