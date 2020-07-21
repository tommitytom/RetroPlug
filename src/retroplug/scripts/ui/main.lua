inspect = require("inspect")
--function prinspect(...) print(inspect(...)) end

require("component")
require("constants")
require("components.ButtonHandler")
require("components.GlobalButtonHandler")
require("Action")
require("Print")

local log = require("log")
log.overridePrint()

local cm = require("ComponentManager")
function _loadComponent(name) cm.loadComponent(name) end

local view = require("View")()
function _getView() return view end
