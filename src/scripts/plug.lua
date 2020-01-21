require("component")
require("constants")
require("components.ButtonHandler")
require("components.GlobalButtonHandler")
local inspect = require("inspect")
local pathutil = require("pathutil")
local serpent = require("serpent")
local json = require("json")

Action = {}
setmetatable(Action, {
	__index = function(table, componentName)
		local actionNameTable = {}
		setmetatable(actionNameTable, {
			__index = function(table, actionName)
				return {
					component = componentName,
					action = actionName
				}
			end
		})

		return actionNameTable
	end
})

local MAX_INSTANCES = 4

Active = nil
local _activeIdx = 0
local _instances = {}
local _keyState = {}

local _globalComponents = {}

local _componentFactory = {
	instance = {},
	global = {}
}

local function componentInputRoute(target, ...)
	local handled = false
	for _, v in ipairs(_globalComponents) do
		local found = v[target]
		if found ~= nil then
			found(v, ...)
			handled = true
		end
	end

	if Active ~= nil then
		for _, v in ipairs(Active.components) do
			local found = v[target]
			if found ~= nil then
				found(v, ...)
				handled = true
			end
		end
	end

	return handled
end

local function findInstanceComponents(emulatorDesc, buttons)
	local components = {}
	for _, v in ipairs(_componentFactory.instance) do
		local d = v.__desc
		if d.romName == nil or emulatorDesc.romName:find(d.romName) ~= nil then
			print("Attaching component " .. d.name)
			table.insert(components, v.new(emulatorDesc, buttons))
		end
	end

	return components
end

local function runComponentHandler(components, handlerName, ...)
	for _, component in ipairs(components) do
		local found = component[handlerName]
		if found ~= nil then found(component, ...) end
	end
end

function _loadComponent(name)
	local component = require(name)
	if component ~= nil then
		print("Registered component: " .. component.__desc.name)
		if component.__desc.global == true then
			table.insert(_componentFactory.global, component)
		else
			table.insert(_componentFactory.instance, component)
		end
	else
		print("Failed to load " .. name .. ": Script does not return a component")
	end
end

function _init()
	for _, v in ipairs(_componentFactory.global) do
		table.insert(_globalComponents, v.new())
	end

	for i = 1, MAX_INSTANCES, 1 do
		local desc = _proxy:getInstance(i - 1)
		if desc.state ~= EmulatorInstanceState.Uninitialized then
			local buttons = _proxy:buttons(i - 1)
			local instance = {
				desc = desc,
				components = findInstanceComponents(desc, buttons),
				buttons = buttons
			}

			table.insert(_instances, instance)

			if i == _proxy:activeInstanceIdx() + 1 then
				_activeIdx = i
				Active = _instances[i]
			end
		end
	end

	for _, instance in ipairs(_instances) do
		runComponentHandler(instance.components, "onComponentsInitialized", instance.components)
		runComponentHandler(instance.components, "onReload", instance.desc)
	end
end

local function addInstance(desc)
	if #_instances < MAX_INSTANCES then
		desc.idx = #_instances

		local instance = {
			desc = desc,
			components = {},
			buttons = _proxy:buttons(desc.idx)
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
		local instance = {
			model = _proxy:duplicateInstance(idx),
			components = {},  -- TODO: Duplicate components
			buttons = _proxy:buttons(idx)
		}

		table.insert(_instances, instance)
		_setActive(#_instances - 1)

		runComponentHandler(instance.components, "onComponentsInitialized", instance.components)
	end
end

local function cloneFields(source, fields, target)
	if target == nil then
		target = {}
	end

	for _, v in ipairs(fields) do
		target[v] = source[v]
	end

	return target
end

function _saveProject(state, pretty)
	local proj = _proxy:getProject()

	local t = {
		version = _RETROPLUG_VERSION,
		path = proj.path,
		settings = cloneFields(proj.settings, { "audioRouting", "midiRouting", "layout", "zoom", "saveType" }),
		instances = {},
		files = {}
	}

	for i = 1, MAX_INSTANCES, 1 do
		local desc = _proxy:getInstance(i - 1)
		if desc.state ~= EmulatorInstanceState.Uninitialized then
			local inst = cloneFields(desc, { "emulatorType", "romPath", "savPath" })
			inst.components = {}

			if state.buffers[i] ~= nil then
				inst.state = base64.encodeBuffer(state.buffers[i], state.sizes[i])
			end

			table.insert(t.instances, inst)
		else
			break
		end
	end

	local opts = { comment = false }
	if pretty == true then opts.indent = '\t' end
	return serpent.dump(t, opts)
end

function _saveProjectToFile(state, pretty)
	local proj = _proxy:getProject()
	local data = _saveProject(state, pretty)
	local file = io.open(proj.path, "w")
	file:write(data)
	file:close()
end

local function loadProject010(projectData)
end

local function loadProject020(projectData)
	local fm = _proxy:fileManager()
	local proj = _proxy:getProject()
	cloneFields(projectData.settings, { "audioRouting", "midiRouting", "layout", "zoom", "saveType" }, proj.settings)
	_proxy:updateSettings()

	for _, inst in ipairs(projectData.instances) do
		local desc = EmulatorInstanceDesc.new()
		desc.idx = -1
		desc.fastBoot = true
		cloneFields(inst, { "emulatorType", "romPath", "savPath" }, desc)

		local romFile = fm:loadFile(inst.romPath, false)
		if romFile ~= nil then
			desc.sourceRomData = romFile.data
			local state = base64.decodeBuffer(inst.state)

			if proj.settings.saveType == SaveStateType.Sram then
				desc.sourceSavData = state
			elseif proj.settings.saveType == SaveStateType.State then
				desc.sourceStateData = state
			end

			_loadRom(desc)
		end
	end
end

function _loadProject(path)
	local file = io.open(path, "r")
	if file == nil then
		error("Failed to load project: Unable to open file")
	end

	local data = file:read("*a")
	local ok, projectData = serpent.load(data)
	if ok ~= true then
		-- Old projects (<= v0.1.0) are encoded using JSON rather than lua
		projectData = json.decode(data)
		if projectData == nil then
			error("Failed to load project: Unable to deserialize file")
		end
	end

	_closeProject()

	_proxy:getProject().path = path

	if projectData.version == "0.1.0" then
		loadProject010(projectData)
	elseif projectData.version == "0.2.0" then
		loadProject020(projectData)
	else
		-- Version not supported!
	end

	_setActive(0)
end

function _loadRomAtPath(idx, romPath, savPath)
	local fm = _proxy:fileManager()
	local romFile = fm:loadFile(romPath, false)
	if romFile == nil then
		return
	end

	local d = EmulatorInstanceDesc.new()
	d.idx = idx
	d.emulatorType = EmulatorType.SameBoy
	d.romPath = romPath
	d.sourceRomData = romFile.data

	if savPath == nil or savPath == "" then
		savPath = pathutil.changeExt(romPath, "sav")
		if fm:exists(savPath) == true then
			local savFile = fm:loadFile(savPath, false)
			if savFile ~= nil then
				d.savPath = savPath
				d.sourceSavData = savFile.data
			end
		end
	end

	_loadRom(d)
end

function _loadRom(desc)
	desc.romName = desc.sourceRomData:slice(0x0134, 15):toString()

	local instance
	if desc.idx == -1 then
		instance = addInstance(desc)
	else
		instance = _instances[desc.idx + 1]
		if instance == nil then
			print("Failed to load rom: Instance " .. desc.idx .. " does not exist")
			return
		end
	end

	instance.components = findInstanceComponents(desc, instance.buttons)

	runComponentHandler(instance.components, "onComponentsInitialized", instance.components)
	runComponentHandler(instance.components, "onBeforeRomLoad", instance.desc)
	_proxy:setInstance(desc)
	runComponentHandler(instance.components, "onRomLoad", instance.desc)
end

function _closeProject()
	_instances = {}
	_activeIdx = 0
	_proxy:closeProject()
end

function _findRom(idx, path)
	--[[std::string originalPath = _proxy->getActiveInstance()->romPath;
	_proxy->instances()
	for (size_t i = 0; i < _views.size(); i++) {
		auto plug = _views[i]->Plug();
		if (plug->romPath() == originalPath) {
			plug->init(paths[0], plug->model(), true);
			plug->disableRendering(false);
			_views[i]->HideText();
		}
	}]]
end

function _setActive(idx)
	_activeIdx = idx + 1
	Active = _instances[_activeIdx]
	_proxy:setActiveInstance(idx)
end

function _frame(delta)

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
	--pluginInputRoute("onMidi", note, down)
end

function _onDrop(str)
	return componentInputRoute("onDrop", str)
end

Action.RetroPlug = {
	NextInstance = function(down)
		if down == true then
			local nextIdx = _activeIdx
			if nextIdx == #_instances then
				nextIdx = 0
			end

			_setActive(nextIdx)
		end
	end
}
