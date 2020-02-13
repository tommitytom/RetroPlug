require("component")
require("constants")
require("components.ButtonHandler")
require("components.GlobalButtonHandler")
require("Action")
require("Print")
local cm = require("ComponentManager")

local inspect = require("inspect")
local pathutil = require("pathutil")
local serpent = require("serpent")
local json = require("json")

local _PROJECT_VERSION = "1.0.0"

local MAX_INSTANCES = 4

Active = nil
local _activeIdx = 0
local _instances = {}
local _keyState = {}

local _globalComponents = {}

--[[local function componentInputRoute(target, ...)
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
end]]

function _loadComponent(name)
	cm.loadComponent(name)
end

function _init()
	cm.createGlobalComponents()

	for i = 1, MAX_INSTANCES, 1 do
		local desc = _proxy:getInstance(i - 1)
		if desc.state ~= EmulatorInstanceState.Uninitialized then
			local buttons = _proxy:buttons(i - 1)
			local instance = {
				desc = desc,
				components = cm.createComponents(desc, desc, buttons),
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
		cm.runAllHandlers("onComponentsInitialized", instance.components, instance.components)
		cm.runAllHandlers("onReload", instance.components, instance.desc)
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

		cm.runAllHandlers("onComponentsInitialized", instance.components, instance.components)
	end
end

local ProjectSettingsFields = {
	audioRouting = AudioChannelRouting,
	midiRouting = MidiChannelRouting,
	layout = InstanceLayout,
	saveType = SaveStateType,
	"zoom"
}

local InstanceSettingsFields = {
	emulatorType = EmulatorType,
	"romPath",
	"savPath"
}

local SameBoySettingsFields = {
	model = GameboyModel,
	"gameLink"
}

local function cloneFields(source, fields, target)
	if target == nil then
		target = {}
	end

	for _, v in ipairs(fields) do
		target[v] = source[v]
	end

	return target
end

local function toEnumString(enumType, value)
	local idx = getmetatable(enumType).__index
	for k, v in pairs(idx) do
		if value == v then return k end
	end
end

local function fromEnumString(enumType, value)
	local v = enumType[value]
	if v ~= nil then
		return v
	end

	local vl = value:sub(1, 1):upper() .. value:sub(2)
	return enumType[vl]
end

local function cloneEnumFields(obj, fields, target)
	if target == nil then target = {} end
	for k, v in pairs(fields) do
		if type(k) == "number" then
			target[v] = obj[v]
		else
			target[k] = toEnumString(v, obj[k])
		end
	end

	return target
end

local function cloneStringFields(obj, fields, target)
	if target == nil then target = {} end
	for k, v in pairs(fields) do
		if type(k) == "number" then
			target[v] = obj[v]
		else
			target[k] = fromEnumString(v, obj[k])
		end
	end

	return target
end

function _saveProject(state, pretty)
	local proj = _proxy:getProject()

	local t = {
		retroPlugVersion = _RETROPLUG_VERSION,
		projectVersion = _PROJECT_VERSION,
		path = proj.path,
		settings = cloneEnumFields(proj.settings, ProjectSettingsFields),
		instances = {},
		files = {}
	}

	for i = 1, MAX_INSTANCES, 1 do
		local desc = _proxy:getInstance(i - 1)
		if desc.state ~= EmulatorInstanceState.Uninitialized then
			local inst = cloneEnumFields(desc, InstanceSettingsFields)
			inst.components = {}
			inst.sameBoy = cloneEnumFields(desc.sameBoySettings, SameBoySettingsFields)

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

local function loadProject_rp010(projectData)
	local fm = _proxy:fileManager()
	local proj = _proxy:getProject()
	cloneStringFields(projectData, ProjectSettingsFields, proj.settings)

	_proxy:updateSettings()

	for _, inst in ipairs(projectData.instances) do
		local desc = EmulatorInstanceDesc.new()
		desc.idx = -1
		desc.fastBoot = true
		cloneStringFields(inst, InstanceSettingsFields, desc)
		cloneStringFields(inst.settings.gameBoy, SameBoySettingsFields, desc.sameBoySettings)
		desc.savPath = inst.lastSramPath

		local romFile = fm:loadFile(inst.romPath, false)
		if romFile ~= nil then
			desc.sourceRomData = romFile.data
			local state = base64.decodeBuffer(inst.state.data)

			if proj.settings.saveType == SaveStateType.Sram then
				desc.sourceSavData = state
			elseif proj.settings.saveType == SaveStateType.State then
				desc.sourceStateData = state
			end

			_loadRom(desc)
		end
	end
end

local function loadProject_100(projectData)
	local fm = _proxy:fileManager()
	local proj = _proxy:getProject()
	cloneStringFields(projectData.settings, ProjectSettingsFields, proj.settings)
	_proxy:updateSettings()

	for _, inst in ipairs(projectData.instances) do
		local desc = EmulatorInstanceDesc.new()
		desc.idx = -1
		desc.fastBoot = true
		cloneStringFields(inst, InstanceSettingsFields, desc)
		cloneStringFields(inst.sameBoy, SameBoySettingsFields, desc.sameBoySettings)

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
		loadProject_rp010(projectData)
	elseif projectData.projectVersion == "1.0.0" then
		loadProject_100(projectData)
	else
		-- Version not supported!
	end

	_setActive(0)
end

function _loadRomAtPath(idx, romPath, savPath, model)
	local fm = _proxy:fileManager()
	local romFile = fm:loadFile(romPath, false)
	if romFile == nil then
		return
	end

	local d = EmulatorInstanceDesc.new()
	d.idx = idx
	d.emulatorType = EmulatorType.SameBoy
	d.state = EmulatorInstanceState.Initialized
	d.romPath = romPath
	d.sourceRomData = romFile.data
	d.sameBoySettings.model = model

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

	instance.components = cm.createComponents(desc, desc, instance.buttons)

	cm.runAllHandlers("onComponentsInitialized", instance.components, instance.components)
	cm.runAllHandlers("onBeforeRomLoad", instance.components, instance.desc)
	_proxy:setInstance(desc)
	cm.runAllHandlers("onRomLoad", instance.components, instance.desc)

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
	local inst = _instances[idx + 1]
	if inst ~= nil and (inst.desc.state == EmulatorInstanceState.Initialized or inst.desc.state == EmulatorInstanceState.Running) then
		_activeIdx = idx + 1
		Active = _instances[_activeIdx]
		_proxy:setActiveInstance(idx)
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
	--pluginInputRoute("onMidi", note, down)
end

function _onDrop(str)
	return componentInputRoute("onDrop", str)
end

function _onMenu()

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
