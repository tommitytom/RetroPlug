--Project = require("Project")
local componentutil = require("util.component")
local ComponentManager = require("ComponentManager")
local Globals = require("Globals")
--
local class = require("class")
local Model = class()

function Model:init()
	self.components = {}

	Project.init()
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
	self.components = ComponentManager.createComponents()

	Project._componentState = componentutil.createState(self.components)

	for _, cfg in pairs(Globals.inputConfigs) do
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

function Model:serialize()
end

return Model
