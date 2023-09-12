require("react-base")

local type EventCallback = function(any): any

local record EventBus
	entities: {integer: {string: EventCallback}}
end

local eventBus: EventBus = {
	entities = {}
}

global function processHostEvent(entity: HostElementHandle, name: string, ev: any): boolean
	local e = eventBus.entities[entity]
	if e then
		local emitter = e[name]
		if emitter then
			emitter(ev)
			return true
		end
	end

	return false
end

local function parseFlexBorder(value: fw.FlexRectT|number, key: string): fw.FlexBorderT
	if type(value) == "number" then
		local v = value as number
		return fw.FlexBorder(v, v, v, v)
	elseif type(value) == "table" then
		local v = value as fw.FlexBorderT
		return fw.FlexBorder(v.left, v.top, v.right, v.bottom)
	elseif type(value) == "userdata" then
		return value as fw.FlexBorderT
	elseif type(value) ~= "nil" then
		print("WARN: Failed to update '" .. key .. "', value of type " .. type(value) .. " is not supported")
	end

	return fw.FlexBorder()
end

local function parseFlexRect(value: fw.FlexRectT|number, key: string): fw.FlexRectT
	if type(value) == "userdata" then
		return value as fw.FlexRectT
	elseif type(value) == "number" then
		print("fw.FlexValue1")
		local v = fw.FlexValue(value as number)
		print("fw.FlexValue2")
		return fw.FlexRect(v, v, v, v)
	elseif type(value) == "table" then
		local v = value as fw.FlexRectT
		return fw.FlexRect(v.left, v.top, v.right, v.bottom)
	elseif type(value) ~= "nil" then
		print("WARN: Failed to update '" .. key .. "', value of type " .. type(value) .. " is not supported")
	end

	return fw.FlexRect()
end

local function parseFlexValue(value: fw.FlexValueT|number, key: string): fw.FlexValueT
	if type(value) == "number" then
		return fw.FlexValue(value as number)
	elseif type(value) == "userdata" then
		return value as fw.FlexValueT
	elseif type(value) ~= "nil" then
		print("WARN: Failed to update '" .. key .. "', value of type " .. type(value) .. " is not supported")
	end

	return fw.FlexValue()
end

local function parseColor4F(value: fw.Color4FT, key: string): fw.Color4FT
	if type(value) == "userdata" then
		return value as fw.Color4FT
	elseif type(value) == "table" then
		local v = value as fw.Color4FT
		return fw.Color4F(v.r, v.g, v.b, v.a)
	elseif type(value) ~= "nil" then
		print("WARN: Failed to update '" .. key .. "', value of type " .. type(value) .. " is not supported")
	end

	return fw.Color4F()
end

local sanitizers: {string:function(any, string): any} = {
	border = parseFlexBorder,
	padding = parseFlexRect,
	margin = parseFlexRect,
	position = parseFlexRect,
	flexBasis = parseFlexValue,
	minWidth = parseFlexValue,
	maxWidth = parseFlexValue,
	minHeight = parseFlexValue,
	maxHeight = parseFlexValue,
	width = parseFlexValue,
	height = parseFlexValue,
	color = parseColor4F,
	backgroundColor = parseColor4F,
}

local reconciler: Reconciler<HostElementHandle, fw.HostStyle, fw.HostEvents> = {
	appendChild = function (node: HostElementHandle, child: HostElementHandle)
		fw.document:appendChild(node, child)
	end,
	removeChild = function (node: HostElementHandle, child: HostElementHandle)
		fw.document:removeChild(node, child)
	end,
	addEventListener = function (node: HostElementHandle, name: string, func: function(any): any)
		local e = eventBus.entities[node]
		if not e then
			e = {}
			eventBus.entities[node] = e
		end

		e[name] = func
		--fw.document:addEventListener(node, child)
	end,
	removeEventListener = function (node: HostElementHandle, name: string, func: function(any): any)
		local e = eventBus.entities[node]
		if e then e[name] = nil end

		--fw.document:removeEventListener(node, child)
	end,
	getStyle = function (node: HostElementHandle): fw.HostStyle
		return fw.document:getStyle(node)
	end,
	createElement = function (tag: string): HostElementHandle
		return fw.document:createElement(tag)
	end,
	createTextNode = function (text: string): HostElementHandle
		return fw.document:createTextNode(text)
	end,
	getRootElement = function (): HostElementHandle
		return fw.document:getRootElement()
	end,
	setProperty = function(node: HostElementHandle, name: string, value: any)
		assert(type(name) == "string")
		local style = fw.document:getStyle(node) as { string: any }

		if value ~= nil then
			local san = sanitizers[name]
			if san then
				value = san(value, name)
			end
		end

		style[name] = value
	end,
	getProperty = function(node: HostElementHandle, name: string): any
		local style = fw.document:getStyle(node) as { string: any }
		return style[name]
	end
}

return reconciler
