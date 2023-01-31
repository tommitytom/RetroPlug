ClickEvent = ButtonClickEvent

local function deepcopy(o, seen)
	seen = seen or {}
	if o == nil then return nil end
	if seen[o] then return seen[o] end

	local no
	if type(o) == 'table' then
	  no = {}
	  seen[o] = no

	  for k, v in next, o, nil do
		no[deepcopy(k, seen)] = deepcopy(v, seen)
	  end
	  setmetatable(no, deepcopy(getmetatable(o), seen))
	else -- number, string, boolean, etc
	  no = o
	end
	return no
end

local Context = {}
function Context:new()
	local o = {
		state = {},
		stateOffset = 1,
		hasMutations = false
	}

	setmetatable(o, self)
	self.__index = self

	return o
end

function Context:useEffect(fn)
end

function Context:useState(default)
	local offset = self.stateOffset
	local stateCtx = self.state[offset]

	if stateCtx == nil then
		stateCtx = {
			value = default, -- deep copy?
			mutations = {},
			setter = nil
		}

		local mutations = {}

		stateCtx.setter = function(v)
			-- deep copy v?
			table.insert(stateCtx.mutations, v)
		end

		self.mutations[offset] = mutations
		self.state[offset] = stateCtx
	end

	self.stateOffset = offset + 1

	return stateCtx.value, stateCtx.setter
end

function Context:applyMutations()
	for _, v in ipairs(self.state) do
		for _, mutation in ipairs(v.mutations) do
			v.value = mutation
		end

		v.mutations = {}
	end

	self.hasMutations = false
end

function Context:resetStateOffset()
	self.stateOffset = 1
end

local function splitProps(desc)
	local ret = {
		props = {},
		events = {}
	}

	for k,v in pairs(desc) do
		local keyType = type(k)
		local valueType = type(v)

		if keyType == "string" then
			if valueType ~= "function" then
				ret.props[k] = v
			else
				ret.events[k] = v
			end
		elseif keyType == "table" then
			if type(k.__typeId) == "function" then
				ret.events[k] = v
			end
		end
	end

	return ret
end

function Component(fn)
	return function (desc)
		local ret = splitProps(desc)
		ret.type = "component"
		ret.func = fn
		ret.dirty = false
		ret.ctx = nil
		ret.instance = nil
		return ret
	end
end

function NativeComponent(fn)
	return function (desc)
		local ret = splitProps(desc)
		ret.type = "native"
		ret.func = fn
		ret.dirty = false
		ret.ctx = nil
		ret.instance = nil
		return ret
	end
end

function Container(desc)
	local components = desc.components
	desc.components = nil

	local ret = splitProps(desc)
	ret.type = "container"
	ret.func = View.new
	ret.components = components
	ret.dirty = false
	ret.ctx = nil
	ret.instance = nil
	return ret
end

local function setNativeProps(instance, props, events)
	for k, v in pairs(props) do
		local propType = type(instance[k])
		local valType = type(v)

		if propType ~= 'nil' then
			if propType == valType then
				instance[k] = v
			else
				print("Failed to set property '" .. instance.__type.name .. "::" .. k .. "': Tried to set property of type " .. propType .. " as a " .. valType)
			end
		else
			print("Failed to set property '" .. instance.__type.name .. "::" .. k .. "' as it does not exist")
		end
	end

	for k, v in pairs(events) do
		if type(k) == "string" then
			error("not supported yet!")
		elseif type(k) == "table" then
			if type(k.__typeId) == "function" then
				uiSelf:subscribe(k, instance, v)
			else
				print("Unrecognised table type used to initialize property")
			end
		else
			print("Unrecognised key type '" .. type(k) .. "' used to initialize property")
		end
	end
end

function CreateState(comp, nativeParent)
	if comp.type == "component" then
		comp.ctx = Context:new()

		comp.instance = comp.fn(comp.props, comp.ctx)

		comp.ctx:applyMutations()
		comp.ctx:resetStateOffset()

		CreateState(comp.instance, nativeParent)
	elseif comp.type == "native" then
		comp.instance = comp.fn()
		nativeParent:addChild(comp.instance)

		setNativeProps(comp.instance, comp.props)
	elseif comp.type == "container" then
		comp.instance = comp.fn()
		nativeParent:addChild(comp.instance)

		setNativeProps(comp.instance, comp.props)

		for _, v in ipairs(comp.components) do
			CreateState(v, comp.instance)
		end
	end
end

local function propagateProps(newComp, oldComp)
	local oldInstance = oldComp.instance

	for k, v in pairs(newComp.props) do
		if oldInstance.props[k] ~= v then
			oldInstance.props[k] = v
			oldInstance.dirty = true
		end
	end

	if newComp.type == "container" then
		for k, v in ipairs(newComp.components) do

		end
	end
end

function UpdateState(comp)
	if comp.dirty == false then
		if comp.type == "container" then
			for _, v in ipairs(comp.components) do
				UpdateState(v)
			end
		end
	else
		if comp.type == "component" then
			comp.ctx:applyMutations()
			comp.dirty = false

			local newComp = comp.fn(comp.props, comp.ctx)

			comp.ctx:resetStateOffset()

			-- Check to see if the child (native) element has changed props
			propagateProps(newComp, comp)

			--UpdateState()
		else
			setNativeProps(comp.instance, comp.props)

			if comp.type == "container" then

			end
		end
	end
end

Panel = NativeComponent(View.new)
Button = NativeComponent(ButtonView.new)

function clamp(v, min, max)
	if v < min then v = min end
	if v > max then v = max end
	return v
end
