local util = require("util")

local _maps = {
	key = {},
	pad = {},
	midi = {}
}

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

local function mergeInputMaps(source, target, actions, romName)
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
	self._actions = {}
	self._buttonHooks = {}
end

function ButtonHandler:onRomLoad()
	self:_updateMaps(self:system():desc())
end

function ButtonHandler:onReload()
	self:_updateMaps(self:system():desc())
end

function ButtonHandler:_updateMaps(desc)
	self._keyMap = { lookup = {}, combos = {} }
	self._padMap = { lookup = {}, combos = {} }
	mergeInputMaps(_maps.key, self._keyMap, self._actions, desc.romName)
	mergeInputMaps(_maps.pad, self._padMap, self._actions, desc.romName)
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
	util.handleInput(self._keyMap, key, down, self._keysPressed, self._buttonHooks, self:system():buttons())
end

function ButtonHandler:onPadButton(button, down)
	util.handleInput(self._padMap, button, down, self._padbuttonsPressed, self._buttonHooks, self:system():buttons())
end

return ButtonHandler
