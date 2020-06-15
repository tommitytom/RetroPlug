local Model = require("Model")
local KeyFilter = require("KeyFilter")
local Menu = require("Menu")
local mainMenu = require("MainMenu")
local createNativeMenu = require("MenuHelper")
local dialog = require("dialog")
local fs = require("fs")

local class = require("class")
local View = class()
function View:init()
	self._keyFilter = KeyFilter()
	self._menuLookup = nil
end

function View:setup(view, audioContext)
	self.view = view;
	self.model = Model(audioContext)
	fs.__setup(audioContext:getFileManager())
	dialog.__setup(view)
end

function View:onKey(key, down)
	if self._keyFilter:onKey(key, down) == true then
		self:emitComponentEvent("onKey", key.vk, down)
	end

	return true
end

function View:onDoubleClick(x, y, mod)
	if #self.model.project.systems == 0 then
		mainMenu.loadProjectOrRom(self.model.project)()
	end
end

function View:onMouseDown(x, y, mod)
	self:selectViewAtPos(x, y)

	if mod.right == true then
		local selectedIdx = self.model.project:getSelectedIndex()
		if selectedIdx > 0 then
			self.model.audioContext:updateSram(selectedIdx - 1)
		end

		local menu = Menu()
		mainMenu.generateMenu(menu, self.model.project)

		for _, comp in ipairs(self.model.project.components) do
			if comp.onMenu ~= nil then comp:onMenu(menu) end
		end

		local selectedSystem = self.model.project:getSelected()
		if selectedSystem ~= nil then
			for _, comp in ipairs(selectedSystem.components) do
				if comp.onMenu ~= nil then
					local valid, ret = pcall(comp.onMenu, comp, menu)
					if valid == false then
						print("Failed to process component menu for " .. comp.__desc.name ..": " .. ret)
					end
				end
			end
		end

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
	print(button, down)
end

function View:onDrop(x, y, items)
	self:selectViewAtPos(x, y)

	local handled = self:emitComponentEvent("onDrop", items)

	if handled == false then
		local selectedSystem = self.model.project:getSelected()
		if selectedSystem ~= nil then
			selectedSystem:emit("onDrop", items, x, y)
		end
	end
end

function View:onReloadBegin()
	self.model.project:serializeComponents()
end

function View:onReloadEnd()

end

function View:saveState(target)
	self.model.project:save(target, false, true)
end

function View:loadState(buffer)
	self.model.project:load(buffer)
end

function View:selectViewAtPos(x, y)
	local idx = self:viewIndexAtPos(x, y)
	if idx ~= nil then
		self.model.project._native.selectedSystem = idx - 1
	end
end

function View:viewIndexAtPos(x, y)
	local pos = Point.new(x, y)
	for i, system in ipairs(self.model.project.systems) do
		if system.desc.area:contains(pos) == true then
			return i
		end
	end
end

function View:emitComponentEvent(name, ...)
	local p = self.model.project

	if p:emit(name, ...) == true then
		return true
	end

	local selected = p:getSelected()
	if selected ~= nil then
		return selected:emit(name, ...)
	end

	return false
end

return View
