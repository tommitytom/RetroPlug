local Project = require("Project")
local componentutil = require("util.component")
local ComponentManager = require("ComponentManager")
--
local class = require("class")
local Model = class()

function Model:init(audioContext, config)
	self.audioContext = audioContext
	self.project = Project(audioContext, config)
	self.components = {}
end

function Model:setup()
	self.components = ComponentManager.createComponents()
	self:emit("onSetup")
	self:emit("onComponentsInitialized", self.components)
	self:emit("onReload")
end

function Model:emit(eventName, ...)
	return componentutil.emitComponentEvent(eventName, self.components, ...)
end

return Model
