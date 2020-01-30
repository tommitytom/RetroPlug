Object = {}

function class(name, base)
	local c = nil
	base = base or Object

	if(_G[name] == nil) then
		c = {}    -- a new class instance
	else
		c = _G[name]   -- modify existing instance :)
	end

	if(base == nil) then error("Base class doesn't exist, make sure your class was included in the right order or derive from Object") end
	-- our new class is a shallow copy of the base class!
	for i,v in pairs(base) do
		c[i] = v
	end
	c._base = base

	-- the class will be the metatable for all its objects,
	-- and they will look up their methods in it.
	c.__index = c
	c._typeName = name

	-- expose a constructor which can be called by <classname>(<args>)
	local mt = {}
	mt.create = function(class_tbl, ...)
		local obj = {}
		setmetatable(obj, c)
		if c.init then
			c.init(obj,...)
		end
		return obj
	end

	c.isA = function(self, klass)
		local m = getmetatable(self)
		return m == klass
	 end

	 setmetatable(c, mt)
	 _G[name] = c
end
