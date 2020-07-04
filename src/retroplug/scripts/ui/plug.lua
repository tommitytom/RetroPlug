--[[local json = require 'dkjson'
local debuggee = require 'vscode-debuggee'
local startResult, breakerType = debuggee.start(json)
print('debuggee start ->', startResult, breakerType)]]

inspect = require("inspect")
function prinspect(...) print(inspect(...)) end

require("component")
require("constants")
require("components.ButtonHandler")
require("components.GlobalButtonHandler")
require("Action")
require("Print")

local serializer = require("serializer")
local cm = require("ComponentManager")
local LuaMenu = require("Menu")
local System = require("System")
local createNativeMenu = require("MenuHelper")
local util = require("util")

local serpent = require("serpent")
local json = require("json")

local _PROJECT_VERSION = "1.0.0"

local MAX_INSTANCES = 4

Active = nil
local _activeIdx = 0
local _instances = {}
local _keyState = {}

function _loadComponent(name)
	cm.loadComponent(name)
end

local pathutil = require("pathutil")
local fs = require("fs")

local function addInstance(desc)
	if #_instances < MAX_INSTANCES then
		desc.idx = #_instances

		local instance = {
			system = System(desc, _proxy:buttons(desc.idx)),
			components = {}
		}

		table.insert(_instances, instance)
		_setActive(#_instances - 1)

		return instance
	end
end

function _removeInstance(index)
	table.remove(_instances, index + 1)
	_proxy:removeInstance(index)

	if _activeIdx > #_instances then
		_setActive(#_instances - 1)
	end
end

function _duplicateInstance(idx)
	if #_instances < MAX_INSTANCES then
		local sourceInst = _instances[idx + 1]

		local componentData = serializer.serializeInstanceToString(sourceInst)

		local desc = _proxy:duplicateInstance(idx)
		local system = System(desc, _proxy:buttons(desc.idx))

		local instance = {
			system = system,
			components = cm.createComponents(system)
		}

		serializer.deserializeInstancesFromString(instance, componentData)

		table.insert(_instances, instance)
		_setActive(desc.idx)

		cm.runAllHandlers("onComponentsInitialized", instance.components, instance.components)
		cm.runAllHandlers("onRomLoad", instance.components, instance.system)
	end
end

function _saveProjectToFile(state, pretty)
	local proj = _proxy:getProject()
	local data = _saveProject(state, pretty)
	fs.saveText(proj.path, data)
end

function _loadProject(path)
	local file = fs.load(path)
	if file == nil then
		print("Failed to load project: Unable to open file at " .. path)
		return
	end

	local data = file:toString()
	local ok, projectData = serpent.load(data)
	if ok ~= true then
		-- Old projects (<= v0.2.0) are encoded using JSON rather than lua
		projectData = json.decode(data)
		if projectData == nil then
			error("Failed to load project: Unable to deserialize file")
		end
	end

	_closeProject()

	_proxy:getProject().path = path

	if projectData.version == "0.1.0" then
		loadProject_rp010(projectData)
	elseif projectData.projectVersion == "1.0.0" then
		loadProject_100(projectData)
	else
		-- Version not supported!
	end

	_setActive(0)
end

function _loadRomAtPath(idx, romPath, savPath, model)
	local d = _proxy:createInstance()
	d.idx = idx
	d.emulatorType = EmulatorType.SameBoy
	d.romPath = romPath
	d.state = EmulatorInstanceState.RomMissing

	d.sameBoySettings.model = model

	if fs.exists(romPath) then
		local romData = fs.load(romPath, false)
		if romData ~= nil then
			d.romData = romData
			d.state = EmulatorInstanceState.Initialized
		end
	end

	if savPath == nil or savPath == "" then
		savPath = pathutil.changeExt(romPath, "sav")
		if fs.exists(savPath) == true then
			local savData = fs.load(savPath, false)
			if savData ~= nil then
				d.savPath = savPath
				d.sramData = savData
			end
		end
	end

	_loadRom(d)
end

function _loadRom(desc)
	local instance
	if desc.idx == -1 then
		instance = addInstance(desc)
	else
		instance = _instances[desc.idx + 1]
		if instance == nil then
			print("Failed to load rom: Instance " .. desc.idx .. " does not exist")
			return
		end

		if desc.sameBoySettings.model == GameboyModel.Auto then
			desc.sameBoySettings.model = instance.system:desc().sameBoySettings.model
		end

		instance = {
			system = System(desc, _proxy:buttons(desc.idx)),
			components = {}
		}

		_instances[desc.idx + 1] = instance
	end

	if desc.romData ~= nil then
		desc.romName = util.getRomName(desc.romData)
		instance.components = cm.createComponents(instance.system)

		cm.runAllHandlers("onComponentsInitialized", instance.components, instance.components)
		cm.runAllHandlers("onBeforeRomLoad", instance.components, instance.system)
		_proxy:setInstance(desc)
		cm.runAllHandlers("onRomLoad", instance.components, instance.system)
	else
		_proxy:setInstance(desc)
	end

	if _activeIdx == 0 then
		_setActive(0)
	end
end

function _closeProject()
	_instances = {}
	_activeIdx = 0
	_proxy:closeProject()
end

function _findRom(idx, path)
	local inst = _instances[idx + 1]
	if inst ~= nil then
		local originalPath = inst.system:desc().romPath
		local romData = fs.load(path)
		if romData ~= nil then
			-- Find all instances that are also missing the same rom
			for _, v in ipairs(_instances) do
				if v.system:desc().romPath == originalPath then
					v.system:loadRom(romData)
				end
			end
		end
	end
end

function _setActive(idx)
	local inst = _instances[idx + 1]

	if inst ~= nil then
		local state = inst.system:desc().state
		if state == EmulatorInstanceState.Initialized or state == EmulatorInstanceState.Running then
			_activeIdx = idx + 1
			Active = _instances[_activeIdx]
			_proxy:setActiveInstance(idx)
		else
			print("Failed to set active.  Instance state is " .. state)
		end
	end
end

function _frame(delta)

end

local function componentInputRoute(name, ...)
	local components
	if Active ~= nil then components = Active.components end
	cm.runAllHandlers(name, components, ...)
end

function _onKey(key, down)
	local vk = key.vk
	if down == true then
		if _keyState[vk] ~= nil then return end
		_keyState[vk] = true
	else
		_keyState[vk] = nil
	end

	return componentInputRoute("onKey", vk, down)
end

function _onPadButton(button, down)
	return componentInputRoute("onPadButton", button, down)
end

function _onMidi(note, down)
	--componentInputRoute("onMidi", note, down)
end

function _onDrop(str)
	return componentInputRoute("onDrop", str)
end

local _menuLookup = nil

function _onMenu(menus)
	local menu = LuaMenu()
	local componentsMenu = menu:subMenu("System"):subMenu("UI Components")

	if Active ~= nil then
		local names = cm.getComponentNamesMap()
		local addMenu = componentsMenu:subMenu("Add")
		componentsMenu:separator()

		for _, comp in ipairs(Active.components) do
			local compActive = cm.isSystemComponent(comp)
			componentsMenu:select(comp.__desc.name, comp:enabled(), function(enabled) comp:setEnabled(enabled) end, compActive)
			names[comp.__desc.name] = nil

			if comp:enabled() == true and comp.onMenu ~= nil then
				comp:onMenu(menu)
			end
		end

		local hasNames = false
		for k, _ in pairs(names) do
			hasNames = true
			addMenu:action(k, function()
				local component = cm.createComponent(k)
				if component ~= nil then
					table.insert(Active.components, component)
					cm.runAllHandlers("onComponentsInitialized", { component }, Active.components)
					cm.runAllHandlers("onRomLoad", { component }, Active.system)
				end
			end)
		end

		addMenu.active = hasNames
	end

	local menuLookup = {}
	menus:add(createNativeMenu(menu, nil, LUA_MENU_ID_OFFSET, menuLookup))
	_menuLookup = menuLookup
end

function _onMenuResult(idx)
	if _menuLookup ~= nil then
		local callback = _menuLookup[idx]
		if callback ~= nil then
			callback()
		end

		_menuLookup = nil
	end
end

function _serializeInstances() return serializer.serializeInstancesToString(_instances) end
function _serializeInstance(idx) return serializer.serializeInstanceToString(_instances[idx + 1]) end
function _deserializeInstances(data) serializer.deserializeInstancesFromString(_instances, data) end


