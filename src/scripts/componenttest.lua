require "component"
local inspect = require "inspect"

local ComponentTest = component({ name = "Test component", global = true })

function ComponentTest:init()
	print("INIT")
end

function ComponentTest:dang()
	print(inspect(self.__desc))
end

local v = ComponentTest.new()
v:dang()
