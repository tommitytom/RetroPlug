local Image = TextureView.new
local Button = ButtonView.new

local Property = {}
function Property:new(value)
	local o = {
		value = value,
		listeners = {}
	}

	setmetatable(o, self)
	self.__index = self

	return o
end

function Property:listen(fn)
	table.insert(self.listeners, fn)
end

function Property:set(value)
	self.value = value
	self:emit()
end

function Property:emit()
	for _, fn in ipairs(self.listeners) do
		fn(self.value)
	end
end


local Conditional = {}
function Conditional:new(valueProp, tr, fa)
	local o = {
		valueProp = valueProp,
		tr = tr,
		fa = fa
	}

	setmetatable(o, self)
	self.__index = self

	return o
end



function ref(v)
	return Property:new(v)
end

function fmt(s, ...)

end

function conditional(prop, tr, fa)
	return Conditional:new(prop, tr, fa)
end

function foreach(prop, fn)

end

local function isProperty(v)
	return type(v) == "table" and getmetatable(v) == Property
end

local function isConditional(v)
	return type(v) == "table" and getmetatable(v) == Conditional
end

function updateProps(obj, props)
	for k, v in pairs(props) do
		if type(k) == "string" then
			if k ~= "views" then
				local propType = type(obj[k])
				local valType = type(v)

				if propType ~= 'nil' then
					if isProperty(v) then
						-- TODO: Type check
						v:listen(function(value) obj[k] = value end)
						v = v.value
						valType = type(v)
					end

					if propType == valType then
						obj[k] = v
					else
						print("Failed to set property '" .. obj.__type.name .. "::" .. k .. "': Tried to set property of type " .. propType .. " as a " .. valType)
					end
				else
					print("Failed to set property '" .. obj.__type.name .. "::" .. k .. "' as it does not exist")
				end
			else
				for _, child in ipairs(v) do
					if isConditional(child) then

					end

					obj:addChild(child)
				end
			end
		elseif type(k) == "table" then
			if type(k.__typeId) == "function" then
				uiSelf:subscribe(k, obj, v)
			else
				print("Unrecognised table type used to initialize property")
			end
		else
			print("Unrecognised key type '" .. type(k) .. "' used to initialize property")
		end
	end
end
