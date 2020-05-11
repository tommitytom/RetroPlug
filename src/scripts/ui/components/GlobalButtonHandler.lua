local GlobalButtonHandler = component({ name = "Global Button Handler", global = true })

local util = require("util")

local _buttonMaps = {
	key = { lookup = {}, combos = {} },
	pad = { lookup = {}, combos = {} },
	midi = { lookup = {}, combos = {} }
}

function GlobalButtonHandler:init()
	self.keysPressed = {}
	self.padButtonsPressed = {}
end

function GlobalButtonHandler:onKey(key, down)
	return util.handleInput(_buttonMaps.key, key, down, self.keysPressed)
end

function GlobalButtonHandler:onPadButton(button, down)
	return util.handleInput(_buttonMaps.pad, button, down, self.padButtonsPressed)
end

function GlobalKeyMap(config, map)
	if map == nil then map = config end
	_buttonMaps.key = util.inputMap(config, map)
end

function GlobalPadMap(config, map)
	if map == nil then map = config end
	_buttonMaps.pad = util.inputMap(config, map)
end

return GlobalButtonHandler
