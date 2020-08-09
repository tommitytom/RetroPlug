local class = require("class")
local const = require("const")
local projectutil = require("util.project")
local serpent = require("serpent")
local serializer = require("serializer")
local log = require("log")
local Timer = require("timer")

local Error = require("Error")
local ComponentManager = require("ComponentManager")
local System = require("System")

local _data = {
	config = nil,
	inputConfigs = nil,
	state = {},
	systems = {},
	selected = nil,
	inputMap = nil
}

local Project = {
	data = _data
}

local _audioContext
local _native
local _buttonHooks = {}

function Project.get_buttonHooks() return _buttonHooks end
function Project.get_settings() return _native.settings end
function Project.get_path() return _native.path end
function Project.get_inputConfigs() return _data.inputConfigs end
function Project.get_inputMap() return _data.inputMap end
function Project.get_systems() return _data.systems end

function Project.init(audioContext, config, inputConfigs)
	_audioContext = audioContext
	_native = audioContext:getProject()

	_data.config = config
	_data.inputConfigs = inputConfigs
	_data.inputMap = inputConfigs["default.lua"]

	projectutil.copyStringFields(config.project, projectutil.ProjectSettingsFields, _native.settings)

	local count = #_native.systems
	for i = 1, count, 1 do
		local desc = _native.systems[i]
		if desc.state ~= SystemState.Uninitialized then
			local system = System(desc, nil)
			system._audioContext = audioContext
			table.insert(_data.systems, system)
		end
	end

	--Project.deserializeComponents()
	if Project.getSelectedIndex() == 0 and count > 0 then Project.setSelected(1) end
end

function Project.loadConfigFromPath(path, updateProject)
	--projectutil.copyStringFields(config.project, projectutil.ProjectSettingsFields, _native.settings)
end

function Project.clear()
	_data.systems = {}
	_audioContext:clearProject()
end

local function addSystem(system)
	local desc = system.desc
	local idx = desc.idx + 1
	if idx == 0 then
		assert(#_data.systems < const.MAX_SYSTEMS)
		idx = #_data.systems + 1
	end

	system._audioContext = _audioContext
	_data.systems[idx] = system

	desc.idx = idx - 1
	_audioContext:setSystem(desc)

	return idx
end

function Project.loadRom(data, idx, model)
	local system = System(data, model, _data.config.system)
	if idx ~= nil then system.desc.idx = idx - 1 end

	idx = addSystem(system)
	if idx ~= 0 then Project.setSelected(idx) end
end

function Project.setSelected(idx)
	if idx <= const.MAX_SYSTEMS then
		_native.selectedSystem = idx - 1
		_data.system = _data.systems[idx]
	end
end

function Project.getSelectedIndex()
	return _native.selectedSystem + 1
end

function Project.getSelected()
	return _data.systems[_native.selectedSystem + 1]
end

function Project.removeSystem(idx)
	if idx ~= 0 then
		table.remove(_data.systems, idx)
		_audioContext:removeSystem(idx - 1)
	else
		print("Failed to remove system: Invalid index " .. idx)
	end
end

function Project.load(data)
	local projectData, systems, err = projectutil.loadProject(data, _data.config)
	if err ~= nil then log.obj(err); return err end
	Project.clear()

	if projectData.path then
		_native.path = projectData.path
	end

	projectutil.copyStringFields(projectData.settings, projectutil.ProjectSettingsFields, _native.settings)
	_audioContext:updateSettings()

	--Project.deserializeComponents()

	for _, system in ipairs(systems) do
		addSystem(system)
	end

	if Project.getSelectedIndex() == 0 and #_data.systems > 0 then Project.setSelected(1) end
end

function Project.save(path, pretty, immediate)
	local timer = Timer()

	if #_data.systems == 0 then return end

	if pretty == nil then pretty = true end
	if immediate == nil then immediate = false end

	_audioContext:fetchSystemStates(immediate, function(systemStates)
		if path == nil then
			path = _native.path
			assert(path ~= "")
		elseif type(path) == "string" then
			_native.path = path
		end

		log.info("Saving project to " .. path)

		local zipSettings = ZipWriterSettings.new()
		zipSettings.method = ZipCompressionMethod.Deflate
		zipSettings.level = ZipCompressionLevel.Normal

		local data = Project.serializeProject(systemStates, _native, pretty)
		local err = projectutil.saveProject(path, data, _data.systems, systemStates, zipSettings)
		if err ~= nil then print(err) end

		timer:log()
	end)
end

function Project.serializeProject(audioSystemStates, projectSettings, pretty)
	local t = {
		retroPlugVersion = _RETROPLUG_VERSION,
		projectVersion = _PROJECT_VERSION,
		path = projectSettings.path,
		settings = projectutil.cloneEnumFields(projectSettings.settings, projectutil.ProjectSettingsFields),
		systems = {}
	}

	for i, system in ipairs(_data.systems) do
		local desc = system.desc
		if desc.state ~= SystemState.Uninitialized then
			local inst = projectutil.cloneEnumFields(desc, projectutil.SystemSettingsFields)
			inst.sameBoy = projectutil.cloneEnumFields(desc.sameBoySettings, projectutil.SameBoySettingsFields)
			inst.uiComponents = serializer.serializeComponents(system.components)

			local ok, audioComponents = serpent.load(audioSystemStates.components[i])
			if ok == true and audioComponents ~= nil then
				inst.audioComponents = audioComponents
			end

			table.insert(t.systems, inst)
		else
			break
		end
	end

	local opts = { comment = false }
	if pretty == true then opts.indent = '\t' end
	return serpent.block(t, opts)
end

function Project.addComponent(componentType)
	if type(componentType) == "string" then
		local component = ComponentManager.createComponent(self, componentType)
		if component ~= nil then
			table.insert(self.components, component)
		end
	end
end

function Project.removeComponent(idx)
	table.remove(self.components, idx)
end

function Project.setComponentEnabled(idx, enabled)
	local component = self.components[idx]
end

-- Serializes all components to a single string that
-- is stored on the native part of the system (SystemDesc).
-- This is useful for storing state between reloads.
function Project.serializeComponents()
	--[[for _, system in ipairs(_data.systems) do
		local systemComponents = {}
		for _, comp in ipairs(system.components) do
			local data = {}
			if comp.onSerialize ~= nil then
				-- TODO: Put this in a pcall?
				data = comp.onSerialize(comp)
			end

			systemComponents[comp.__desc.name] = data
		end

		system.desc.uiComponentState = serpent.dump(systemComponents)
	end]]
end

function Project.deserializeComponents()
	for _, system in ipairs(_data.systems) do
		if #system.desc.uiComponentState > 0 then
			local ok, systemComponents = serpent.load(system.desc.uiComponentState)
			if ok and systemComponents then
				for _, comp in ipairs(system.components) do
					local compData = systemComponents[comp.__desc.name]
					if compData and comp.onDeserialize then
						-- TODO: Put this in a pcall?
						comp.onDeserialize(comp, compData)
					end
				end
			end

			system.desc.uiComponentState = ""
		end
	end
end



function Project.duplicateSystem(idx)
	assert(#_data.systems < const.MAX_SYSTEMS)
	local system = _data.systems[idx]
	local newSystem = system:clone()
	local newIdx = #_data.systems

	newSystem._audioContext = _audioContext
	newSystem.desc.idx = newIdx
	table.insert(_data.systems, newSystem)

	_audioContext:duplicateSystem(idx - 1, newSystem.desc)

	Project.setSelected(newIdx + 1)
end

function Project.removeSystem(idx)
	_audioContext:removeSystem(idx - 1)
	table.remove(_data.systems, idx)
end

function Project.nextSystem()
	local idx = Project.getSelectedIndex()
	if idx == #_data.systems then
		idx = 1
	else
		idx = idx + 1
	end

	Project.setSelected(idx)
end

local _getters = {}
local _setters = {}
local util = require("util")

for k, v in pairs(Project) do
	if util.startsWith(k, "get_") then
		_getters[k:sub(5)] = v
	end

	if util.startsWith(k, "set_") then
		_setters[k:sub(5)] = v
	end
end

setmetatable(Project, {
	__index = function(obj, key)
		local prop = _getters[key]
		if prop ~= nil then
			return prop()
		else
			return rawget(obj, key)
		end
	end,
	__newindex = function(obj, key, value)
		local prop = _setters[key]
		if prop ~= nil then
			return prop(value)
		else
			return rawset(obj, key, value)
		end
	end
})

return Project
