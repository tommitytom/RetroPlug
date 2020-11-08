inspect = require("inspect")
log = require("log")

log.info("SETTING PROJECT")
_G["Project"] = require("Project")
log.info("SET PROJECT")

require("component")
require("constants")
require("Print")

log.overridePrint()

--[[setmetatable(_G, {
	__newindex = function (_, n)
		error("attempt to write to undeclared variable "..n, 2)
	end,
	__index = function (_, n)
		error("attempt to read undeclared variable "..n, 2)
	end,
})]]

local cm = require("ComponentManager")
function _loadComponent(name)
	cm.loadComponent(name)
end

function _getView()
	return require("View")()
end
