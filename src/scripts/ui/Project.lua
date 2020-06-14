local class = require("class")
local const = require("const")
local projectutil = require("util.project")
local componentutil = require("util.component")
local serpent = require("serpent")
local serializer = require("serializer")

local Error = require("Error")
local ComponentManager = require("ComponentManager")
local System = require("System")

local Project = class()
function Project:init(audioContext)
	self._audioContext = audioContext
	self._native = audioContext:getProject()
	self.components = ComponentManager.createProjectComponents(self)
	self.systems = {}

	local count = #self._native.systems
	for i = 1, count, 1 do
		local desc = self._native.systems[i]
		if desc.state ~= SystemState.Uninitialized then
			local system = System(desc)
			system._audioContext = self._audioContext
			table.insert(self.systems, system)
		end
	end

	self:deserializeComponents()

	self:emit("onComponentsInitialized", self.components)
	self:emit("onSetup")

	for _, system in ipairs(self.systems) do
		system:emit("onComponentsInitialized", system.components)
		system:emit("onReload")
	end

	if self:getSelectedIndex() == 0 and count > 0 then self:setSelected(1) end
end

function Project:emit(eventName, ...)
	return componentutil.emitComponentEvent(eventName, self.components, ...)
end

function Project:clear()
	-- TODO: Recreate components on clear?
	--self.components = ComponentManager.createProjectComponents(self)
	self.systems = {}
	self._audioContext:clearProject()
end

function Project:loadRom(data, idx, model)
	local system = System(data, model)
	if idx ~= nil then system.desc.idx = idx - 1 end

	idx = self:addSystem(system)
	if idx ~= 0 then self:setSelected(idx) end
end

function Project:setSelected(idx)
	self._native.selectedSystem = idx - 1
end

function Project:getSelectedIndex()
	return self._native.selectedSystem + 1
end

function Project:getSelected()
	return self.systems[self._native.selectedSystem + 1]
end

function Project:removeSystem(idx)
	if idx ~= 0 then
		table.remove(self.systems, idx)
		self._audioContext:removeSystem(idx - 1)
	else
		print("Failed to remove system: Invalid index " .. idx)
	end
end

function Project:load(data)
	local projectData, systems, err = projectutil.loadProject(data)
	if err ~= nil then prinspect(err); return err end
	self:clear()

	self._native.path = projectData.path
	projectutil.copyStringFields(projectData.settings, projectutil.ProjectSettingsFields, self._native.settings)
	self._audioContext:updateSettings()

	self.components = ComponentManager.createProjectComponents(self)

	self:deserializeComponents()

	for _, system in ipairs(systems) do
		self:addSystem(system)
	end

	self:emit("onComponentsInitialized", self.components)
	self:emit("onSetup")

	for _, system in ipairs(self.systems) do
		system:emit("onComponentsInitialized", system.components)
		system:emit("onReload")
	end

	if self:getSelectedIndex() == 0 and #self.systems > 0 then self:setSelected(1) end
end

function Project:save(path, pretty)
	self._audioContext:fetchSystemStates(function(systemStates)
		if path == nil then
			path = self._native.path
			assert(path ~= "")
		else
			self._native.path = path
		end

		local data = self:serializeProject(systemStates, self._native, pretty)
		local err = projectutil.saveProject(path, data, self.systems, systemStates)
		if err ~= nil then print(err) end
	end)
end

function Project:serializeProject(audioSystemStates, projectSettings, pretty)
	local t = {
		retroPlugVersion = _RETROPLUG_VERSION,
		projectVersion = _PROJECT_VERSION,
		path = projectSettings.path,
		settings = projectutil.cloneEnumFields(projectSettings.settings, projectutil.ProjectSettingsFields),
		systems = {}
	}

	for i, system in ipairs(self.systems) do
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
	return serpent.dump(t, opts)
end

function Project:addComponent(componentType)
	if type(componentType) == "string" then
		local component = ComponentManager.createComponent(self, componentType)
		if component ~= nil then
			table.insert(self.components, component)
		end
	end
end

function Project:removeComponent(idx)
	table.remove(self.components, idx)
end

function Project:setComponentEnabled(idx, enabled)
	local component = self.components[idx]
end

-- Serializes all components to a single string that
-- is stored on the native part of the system (SystemDesc).
-- This is useful for storing state between reloads.
function Project:serializeComponents()
	for _, system in ipairs(self.systems) do
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
	end
end

function Project:deserializeComponents()
	for _, system in ipairs(self.systems) do
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

function Project:addSystem(system)
	local desc = system.desc
	local idx = desc.idx + 1
	if idx == 0 then
		assert(#self.systems < const.MAX_INSTANCES)
		idx = #self.systems + 1
	end

	system._audioContext = self._audioContext
	self.systems[idx] = system

	desc.idx = idx - 1
	self._audioContext:setSystem(desc)

	return idx
end

function Project:duplicateSystem(idx)
	assert(#self.systems < const.MAX_INSTANCES)
	local system = self.systems[idx]
	local newSystem = system:clone()
	local newIdx = #self.systems

	newSystem._audioContext = self._audioContext
	newSystem.desc.idx = newIdx
	table.insert(self.systems, newSystem)

	self._audioContext:duplicateSystem(idx - 1, newSystem.desc)

	for _, system in ipairs(self.systems) do
		system:emit("onComponentsInitialized", system.components)
		system:emit("onReload")
	end

	self:setSelected(newIdx + 1)
end

function Project:removeSystem(idx)
	self._audioContext:removeSystem(idx - 1)
	table.remove(self.systems, idx)
end

function Project:nextSystem()
	local idx = self:getSelectedIndex()
	if idx == #self.systems then
		idx = 1
	else
		idx = idx + 1
	end

	self:setSelected(idx)
end

return Project
