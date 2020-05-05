local class = require("class")
local const = require("const")
local fs = require("fs")
local fileutil = require("util.file")
local projectutil = require("util.project")
local serpent = require("serpent")

local Error = require("Error")
local System = require("System")

local Project = class()
function Project:init(audioContext)
	-- TODO: Create project components (cm.createGlobalComponents())

	self._audioContext = audioContext
	self._project = audioContext:getProject()
	self:clear()

	local count = #self._project.systems
	for i = 1, count, 1 do
		local desc = self._project.systems[i - 1]
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

function Project:clear()
	self._components = {}
	self.systems = {}
	self._project:clear()
end

function Project:load(data)
	local projectData = projectutil.loadProject(data)
	local fileData, err = fileutil.loadPathOrData(data)
	if err ~= nil then return err end

	local stringData = fileData:toString()
	local ok, projectData = serpent.load(stringData)
	if ok ~= true then
		-- Old projects (<= v0.2.0) are encoded using JSON rather than lua
		projectData = json.decode(stringData)
		if projectData == nil then
			return Error("Failed to load project: Unable to deserialize file")
		end
	end

	err = validateAndUpgradeProject(projectData)
	if err ~= nil then return err end

	return loadProject_100(projectData)
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
