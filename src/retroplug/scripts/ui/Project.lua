local const = require("const")
local projectutil = require("util.project")
local propertyutil = require("util.property")
local inpututil = require("util.input")
local log = require("log")
local util = require("util")
local Timer = require("timer")
local GameboySystem = require("System")
local Globals = require("Globals")
local Serializer = require('project.serialize')

local _data = {
	--state = {},
	systems = {},
	selected = nil
}

local Project = {
	data = _data
}

local _ctx
local _native
local _buttonHooks = {}

function Project.get_buttonHooks() return _buttonHooks end
function Project.get_systems() return _data.systems end
function Project.get_settings() return _native.settings end
function Project.get_path() return _native.path end
function Project.get_inputConfigs() return Globals.inputConfigs end
function Project.get_inputMap() return Globals.inputMap end

function Project.init()
	_ctx = Globals.audioContext
	_native = _ctx:getProject()

	projectutil.copyStringFields(Globals.config.project, projectutil.ProjectSettingsFields, _native.settings)

	local count = #_native.systems
	for i = 1, count, 1 do
		local desc = _native.systems[i]

		if desc.state ~= SystemState.Uninitialized then
			local system = GameboySystem.fromSystemDesc(desc)

			system:setInputMap(inpututil.getInputMap(Globals.inputConfigs, {
				key = desc.keyInputConfig,
				pad = desc.padInputConfig
			}))

			table.insert(_data.systems, system)
		end
	end

	--Project.deserializeComponents()
	if Project.getSelectedIndex() == 0 and count > 0 then Project.setSelected(1) end
end

function Project.clear()
	_data.systems = {}
	_ctx:clearProject()
	System = nil
end

local function setSystem(system)
	local desc = system.desc
	local idx = desc.idx + 1

	if idx == 0 then
		assert(#_data.systems < const.MAX_SYSTEMS)
		idx = #_data.systems + 1
	end

	local state = util.deepcopy(Project._componentState)
	util.mergeObjects(system.state, state)

	_data.systems[idx] = system

	desc.idx = idx - 1

	_ctx:addSystem(desc)

	return idx
end

function Project.addSystem(systemType)
	assert(#_data.systems < 4)

	local desc = SystemDesc.new()
	desc.idx = #_data.systems
	desc.systemType = systemType or SystemType.SameBoy

	local system = GameboySystem.fromSystemDesc(desc)
	_data.systems[desc.idx + 1] = system

	system.state = util.deepcopy(Project._componentState)
	system:setInputMap(inpututil.getInputMap(Globals.inputConfigs, Globals.config.system.input))

	_ctx:addSystem(desc)

	Project.setSelected(desc.idx + 1)

	return system
end

function Project.setSelected(idx)
	if idx <= const.MAX_SYSTEMS then
		_native.selectedSystem = idx - 1
		_data.system = _data.systems[idx]
		_ctx:updateSelected()
		System = _data.system
	else
		System = nil
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
		_ctx:removeSystem(idx - 1)
	else
		log.error("Failed to remove system: Invalid index " .. idx)
	end
end

function Project.load(data)
	local projectData, systems, err = projectutil.loadProject(data, Globals.config)
	if err ~= nil then log.error(err.msg); return err end
	Project.clear()

	if projectData.path then
		_native.path = projectData.path
	end

	projectutil.copyStringFields(projectData.settings, projectutil.ProjectSettingsFields, _native.settings)
	_ctx:updateSettings()

	for _, system in ipairs(systems) do
		for k, v in pairs(Project._componentState) do
			if system.state[k] == nil then
				system.state[k] = util.deepcopy(v)
			end
		end

		setSystem(system)
	end

	if Project.getSelectedIndex() == 0 and #_data.systems > 0 then Project.setSelected(1) end
end

function Project.save(path, pretty, immediate)
	local timer = Timer()

	if #_data.systems == 0 then return end

	if pretty == nil then pretty = true end
	if immediate == nil then immediate = false end

	_ctx:fetchSystemStates(immediate, function(audioSystemStates)
		if path == nil then
			path = _native.path
			assert(path ~= "")
		elseif type(path) == "string" then
			_native.path = path
		end

		if type(path) == "string" then
			log.info("Saving project to " .. path)
		end

		local zipSettings = ZipWriterSettings.new()
		zipSettings.method = ZipCompressionMethod.Deflate
		zipSettings.level = ZipCompressionLevel.Normal

		local data = Serializer.serializeProject(_data.systems, audioSystemStates, _native, pretty)
		local err = projectutil.saveProject(path, data, _data.systems, audioSystemStates, zipSettings, Project.settings.includeRom)
		if err ~= nil then log.obj(err) end

		timer:log()
	end)
end

function Project.duplicateSystem(idx)
	assert(#_data.systems < const.MAX_SYSTEMS)
	local system = _data.systems[idx]
	local newSystem = system:clone()
	local newIdx = #_data.systems

	newSystem._ctx = _ctx
	newSystem.desc.idx = newIdx
	newSystem.inputMap = { key = system.inputMap.key, pad = system.inputMap.pad }
	table.insert(_data.systems, newSystem)

	_ctx:duplicateSystem(idx - 1, newSystem.desc)

	Project.setSelected(newIdx + 1)
end

function Project.removeSystem(idx)
	_ctx:removeSystem(idx - 1)
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

propertyutil.setupProperties(Project)

return Project
