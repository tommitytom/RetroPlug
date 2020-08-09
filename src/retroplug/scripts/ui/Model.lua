Project = require("Project")
local componentutil = require("util.component")
local ComponentManager = require("ComponentManager")
--
local class = require("class")
local Model = class()

function Model:init(audioContext, config, inputConfigs)
	self.audioContext = audioContext
	self.components = {}

	Project.init(audioContext, config, inputConfigs)
end

local function updateInputActions(components, mapGroup)
	for _, map in ipairs(mapGroup) do
		for _, v in pairs(map.lookup) do
			if type(v) == "table" then
				v.func = componentutil.findAction(components, v.component, v.action)
			end
		end

		for _, v in pairs(map.combos) do
			if type(v) == "table" then
				v.func = componentutil.findAction(components, v.component, v.action)
			end
		end
	end
end

function Model:setup()
	self.components = ComponentManager.createComponents(Project)

	for _, cfg in pairs(Project.inputConfigs) do
		updateInputActions(self.components, cfg.key.global)
		updateInputActions(self.components, cfg.key.system)
		updateInputActions(self.components, cfg.pad.global)
		updateInputActions(self.components, cfg.pad.system)
	end

	for _, component in ipairs(self.components) do
		if component.onBeforeButton ~= nil then
			table.insert(Project.buttonHooks, component.onBeforeButton)
		end
	end

	self:emit("onSetup")
	self:emit("onComponentsInitialized", self.components)
	self:emit("onReload")
end

function Model:emit(eventName, ...)
	return componentutil.emitComponentEvent(self.components, eventName, ...)
end

return Model
