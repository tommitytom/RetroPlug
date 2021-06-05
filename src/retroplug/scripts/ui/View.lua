local Model = require("Model")
local KeyFilter = require("KeyFilter")
local Menu = require("Menu")
local mainMenu = require("MainMenu")
local createNativeMenu = require("MenuHelper")
local dialog = require("dialog")
local fs = require("fs")
local ConfigLoader = require("ConfigLoader")
local class = require("class")
local InputConfig = require("InputConfigParser")
local Globals = require("Globals")

local View = class()
function View:init()
	self._keyFilter = KeyFilter()
	self._menuLookup = nil
	self._config = nil
	self._inputConfig = InputConfig()
end

function View:setup(view, audioContext)
	Globals.audioContext = audioContext

	self.view = view
	fs.__setup(audioContext:getFileManager())
	dialog.__setup(view)
end

function View:loadConfigFromPath(path)
	local ok, config = ConfigLoader.loadConfigFromPath(path)
	if ok == true then
		self._config = config
		Globals.config = config
		return true
	end

	return false
end

function View:loadConfigFromString(str)
	local ok, config = ConfigLoader.loadConfigFromString(str)
	if ok == true then
		self._config = config
		Globals.config = config
		return true
	end

	return false
end

function View:initProject()
	table.sort(self._inputConfig.configs, function(a, b)
		if a.config.name == "Default" then return true end
		if b.config.name == "Default" then return false end
		return string.lower(a.config.name) < string.lower(b.config.name)
	end)

	Globals.inputConfigs = self._inputConfig.configs

	self.model = Model()
	self.model:setup()
end

function View:onKey(key, down)
	if self._keyFilter:onKey(key, down) == true then
		self.model:emit("onKey", key, down)
	end

	return true
end

function View:onDoubleClick(x, y, mod)
	if #Project.systems == 0 then
		mainMenu.loadProjectOrRom()()
	else
		self.model:emit("onDoubleClick", x, y, mod)
	end
end

function View:onMouseDown(x, y, mod)
	self:selectViewAtPos(x, y)

	self.model:emit("onMouseDown", x, y, mod)

	if mod.right == true then
		local selectedIdx = Project.getSelectedIndex()
		if selectedIdx > 0 then
			local selected = Project.getSelected()
			if selected.desc.state ~= SystemState.RomMissing then
				Globals.audioContext:updateSram(selectedIdx - 1)
			end
		end

		local menu = Menu()
		mainMenu.generateMenu(menu)

		self.model:emit("onMenu", menu)

		self._menuLookup = {}
		local nativeMenu = createNativeMenu(menu, nil, LUA_MENU_ID_OFFSET, self._menuLookup, true)
		local audioMenus = Globals.audioContext:onMenu(selectedIdx - 1)

		if #audioMenus > 0 then
			nativeutil.mergeMenu(audioMenus[1], nativeMenu)
		end

		self.view:requestMenu(nativeMenu)
	end
end

function View:onMenuResult(idx)
	if self._menuLookup ~= nil then
		local callback = self._menuLookup[idx]
		if callback ~= nil then callback() end
		self._menuLookup = nil
	end
end

function View:onDialogResult(paths)
	dialog.__onResult(paths)
end

function View:onPadButton(button, down)
	self.model:emit("onPadButton", button, down)
end

function View:onDrop(x, y, items)
	self:selectViewAtPos(x, y)
	self.model:emit("onDrop", items, x, y)
end

function View:onReloadBegin()

end

function View:onReloadEnd()

end

function View:onFrame(delta)
	local releases = self._keyFilter:getKeyReleases(delta)

	if releases then
		for _, v in ipairs(releases) do
			self.model:emit("onKey", v, false)
		end
	end
end

function View:saveState(target)
	Project.save(target, false, true)
end

function View:loadState(buffer)
	Project.load(buffer)
end

function View:selectViewAtPos(x, y)
	local idx = self:viewIndexAtPos(x, y)
	if idx ~= nil then
		Project.setSelected(idx)
	end
end

function View:viewIndexAtPos(x, y)
	local pos = Point.new(x, y)
	for i, system in ipairs(Project.systems) do
		if system._desc.area:contains(pos) == true then
			return i
		end
	end
end

function View:loadInputConfig(path)
	self._inputConfig:load(path)
end

function View:loadInputConfigFromString(name, code)
	self._inputConfig:loadFromString(name, code)
end

return View
