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

--[[Action.RetroPlug = {
	NextInstance = function(down)
		if down == true then
			local nextIdx = _activeIdx
			if nextIdx == #_instances then
				nextIdx = 0
			end

			_setActive(nextIdx)
		end
	end
}]]