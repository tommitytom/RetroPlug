inspect = require("inspect")
log = require("log")

require("component")
require("constants")
require("Print")

Project = require("Project")

log.overridePrint()

local cm = require("ComponentManager")
function _loadComponent(name) cm.loadComponent(name) end

local controller = require("Controller")()
function _getController() return controller end
