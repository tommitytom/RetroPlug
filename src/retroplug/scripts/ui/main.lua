inspect = require("inspect")
log = require("log")

require("component")
require("constants")
require("Print")

--[[setmetatable(_G, {
	__newindex = function (_, n)
		error("attempt to write to undeclared variable "..n, 2)
	end,
	__index = function (_, n)
		error("attempt to read undeclared variable "..n, 2)
	end,
})]]

local log = require("log")
log.overridePrint()

local cm = require("ComponentManager")
function _loadComponent(name) cm.loadComponent(name) end

local view = require("View")()
function _getView() return view end
