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

local View = class()
function View:init()
	self._keyFilter = KeyFilter()
	self._menuLookup = nil
	self._config = nil
	self._inputConfig = InputConfig()
end

function View:setup(view, audioContext)
	self.view = view;
	fs.__setup(audioContext:getFileManager())
	dialog.__setup(view)
end

function View:loadConfigFromPath(path)
	self._config = nil

	local ok, config = ConfigLoader.loadConfigFromPath(path)
	if ok then
		self._config = config
		return true
	end

	return false
end

function View:initProject(audioContext)
	self.model = Model(audioContext, self._config, self._inputConfig.configs)
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
		mainMenu.loadProjectOrRom(Project)()
	end
end

function View:onMouseDown(x, y, mod)
	self:selectViewAtPos(x, y)

	if mod.right == true then
		local selectedIdx = Project.getSelectedIndex()
		if selectedIdx > 0 then
			self.model.audioContext:updateSram(selectedIdx - 1)
		end

		local menu = Menu()
		mainMenu.generateMenu(menu, Project)

		self.model:emit("onMenu", menu)

		self._menuLookup = {}
		local nativeMenu = createNativeMenu(menu, nil, LUA_MENU_ID_OFFSET, self._menuLookup, true)
		local audioMenus = self.model.audioContext:onMenu(selectedIdx - 1)

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
	Project.serializeComponents()
end

function View:onReloadEnd()

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
		--Project.native.selectedSystem = idx - 1
	end
end

function View:viewIndexAtPos(x, y)
	local pos = Point.new(x, y)
	for i, system in ipairs(Project.systems) do
		if system.desc.area:contains(pos) == true then
			return i
		end
	end
end

function View:loadInputConfig(path)
	self._inputConfig:load(path)
end

return View
