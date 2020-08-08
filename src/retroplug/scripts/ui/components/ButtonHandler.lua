--local util = require("util")
local inpututil = require("util.input")
local pathutil = require("pathutil")
local log = require("log")
local util = require("util")
local buttonstring = require("util.buttonstring")



local ButtonHandler = component({ name = "Button Handler", version = "1.0.0", system = true })
function ButtonHandler:init()
	self._keysPressed = {}
	self._padbuttonsPressed = {}
	self._keyMap = { lookup = {}, combos = {} }
	self._padMap = { lookup = {}, combos = {} }
	self._actions = {}
	self._buttonHooks = {}
end

local function populateKeyboardMenu(menu)
	for _, k in ipairs(_sortedLookup) do
		local name = buttonstring.formatKeymapName(k, _mapLookup[k])
		menu:action(name)
	end
end

function ButtonHandler:onMenu(menu)
	local settingsRoot = menu:subMenu("Settings")
	populateKeyboardMenu(settingsRoot:subMenu("Keyboard"))
end

function ButtonHandler:onRomLoad()
	self:_updateMaps(self:system().desc.romName)
end

function ButtonHandler:onReload()
	self:_updateMaps(self:system().desc.romName)
end

function ButtonHandler:_updateMaps(romName)
	print("function ButtonHandler:_updateMaps(romName)")
	if not _sortedLookup then
		_sortedLookup = {}
		for k, v in pairs(_mapLookup) do
			table.insert(_sortedLookup, k)
		end

		table.sort(_sortedLookup)
	end

	self._keyMap = { lookup = {}, combos = {} }
	self._padMap = { lookup = {}, combos = {} }
	inpututil.mergeInputMaps(_maps.key, self._keyMap, self._actions, romName)
	inpututil.mergeInputMaps(_maps.pad, self._padMap, self._actions, romName)
end

--[[function ButtonHandler:onComponentsInitialized(components)
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
end]]



return ButtonHandler
