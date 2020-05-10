local class = require("class")
local const = require("const")
local fs = require("fs")
local fileutil = require("util.file")
local pathutil = require("pathutil")
local projectutil = require("util.project")
local componentutil = require("util.component")
local serpent = require("serpent")

local Error = require("Error")
local System = require("System")

local Project = class()
function Project:init(audioContext)
	-- TODO: Create project components (cm.createGlobalComponents())

	self._audioContext = audioContext
	self._native = audioContext:getProject()
	self._components = {}
	self.systems = {}

	local count = #self._native.systems
	for i = 1, count, 1 do
		local desc = self._native.systems[i]
		if desc.state ~= SystemState.Uninitialized then
			local system = System(desc)
			system._audioContext = self._audioContext
			table.insert(self.systems, system)

			-- TODO: Initialize components!
			-- TODO: Set active
		end
	end

	--[[for _, instance in ipairs(_instances) do
		instance:triggerEvent("onComponentsInitialized", instance.components)
		instance:triggerEvent("onReload")
	end]]
end

function Project:emit(eventName, ...)
	componentutil.emitComponentEvent(eventName, self._components, ...)
end

function Project:clear()
	self._components = {}
	self.systems = {}
	self._audioContext:clearProject()
end

function Project:loadRom(data, idx, model)
	local fileData, err = fileutil.loadPathOrData(data)
	if err ~= nil then return err end

	local romPath
	if type(data) == "string" then romPath = data end

	local d = SystemDesc.new()
	if type(data) == "string" then d.romPath = data end
	if idx ~= nil then d.idx = -1 end
	if model ~= nil then d.sameBoySettings.model = GameboyModel.Auto end

	d.emulatorType = SystemType.SameBoy
	d.state = SystemState.Initialized
	d.sourceRomData = fileData

	if romPath ~= nil then
		local savPath = pathutil.changeExt(romPath, "sav")
		if fs.exists(savPath) == true then
			local savData = fs.load(savPath, false)
			if savData ~= nil then
				d.savPath = savPath
				d.sourceSavData = savData
			end
		end
	end

	self:addSystem(System(d))
end

function Project:setSelected(idx)
	self._native.selectedSystem = idx
end

function Project:load(data)
	local projectData, err = projectutil.loadProject(data)
	if err ~= nil then return err end
	return self:deserializeProject(projectData)
end

function Project:deserializeProject(projectData)
	projectutil.cloneStringFields(projectData.settings, projectutil.ProjectSettingsFields, self._native.settings)
	self._audioContext:updateSettings()

	for _, inst in ipairs(projectData.instances) do
		local system = SystemDesc.new()
		system.idx = -1
		system.fastBoot = true
		projectutil.cloneStringFields(inst, projectutil.InstanceSettingsFields, system)
		projectutil.cloneStringFields(inst.sameBoy, projectutil.SameBoySettingsFields, system.sameBoySettings)

		local romFile = fs.load(inst.romPath, false)
		if romFile ~= nil then
			system.sourceRomData = romFile
		end

		local state = base64.decodeBuffer(inst.state)
		if self._native.settings.saveType == SaveStateType.Sram then
			system.sourceSavData = state
		elseif self._native.settings.saveType == SaveStateType.State then
			system.sourceStateData = state
		end

		system.audioComponentState = serpent.dump(inst.audioComponents)

		self:addSystem(System(system))
	end
end

function Project:save()
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

		for i, instance in ipairs(_instances) do
			local desc = instance.system:desc()
			if desc.state ~= SystemState.Uninitialized then
				local inst = cloneEnumFields(desc, InstanceSettingsFields)
				inst.sameBoy = cloneEnumFields(desc.sameBoySettings, SameBoySettingsFields)
				inst.uiComponents = serializer.serializeInstance(instance)

				local ok, audioComponents = serpent.load(state.components[i])
				if ok == true and audioComponents ~= nil then
					inst.audioComponents = audioComponents
				end

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
end

function Project:addComponent()
end

function Project:removeComponent()
end

function Project:setComponentEnabled(idx, enabled)
end

function Project:addSystem(system)
	local desc = system.desc
	if desc.idx == -1 then
		assert(#self.systems < const.MAX_INSTANCES)
		desc.idx = #self.systems
	end

	system._audioContext = self._audioContext
	table.insert(self.systems, system)

	self._audioContext:setSystem(desc)
end

function Project:duplicateSystem(idx)
	assert(#self.systems < const.MAX_INSTANCES)
	local system = self.systems[idx]
	local newSystem = system:clone()

	system._audioContext = self._audioContext
	table.insert(self.systems, system)

	self._audioContext:duplicateSystem(idx - 1, newSystem.desc)
end

function Project:removeSystem(idx)
	self._audioContext:removeSystem(idx - 1)
	table.remove(self.systems, idx)
end

return Project
