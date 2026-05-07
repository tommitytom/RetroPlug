require("react-base")

local type EventCallback = function(any): any

local record EventBus
	entities: {integer: {string: EventCallback}}
end

local eventBus: EventBus = {
	entities = {}
}

local type NativeStyle = {string:function(any, any)}
local type StyleSetterFunc = function(NativeStyle, string, any);

global function processHostEvent(entity: integer, name: string, ev: any): boolean
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
		local v = fw.FlexValue(value as number)
		return fw.FlexRect(v, v, v, v)
	elseif type(value) == "table" then
		local v = value as fw.FlexRectT
		return fw.FlexRect(v.left, v.top, v.right, v.bottom)
	elseif type(value) ~= "nil" then
		print("WARN: Failed to update '" .. key .. "', value of type " .. type(value) .. " is not supported")
	end

	return fw.FlexRect()
end

local function endswith(str: string, suffix: string): boolean
    return str:sub(-#suffix) == suffix
end

local function parseFlexValue(value: fw.FlexValueT|number|string, key: string): fw.FlexValueT
	if type(value) == "number" then
		return fw.FlexValue(value as number)
	elseif type(value) == "userdata" then
		return value as fw.FlexValueT
	elseif type(value) == "string" then
		local v = value as string
		if endswith(v, "%") then
			return fw.FlexValue(fw.FlexUnit.Percent, tonumber(v:sub(1, #v - 1)))
		end
	end

	if type(value) ~= "nil" then
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

local function parseLengthValue(value: fw.LengthValueT, key: string): fw.LengthValueT
	if type(value) == "userdata" then
		return value as fw.LengthValueT
	elseif type(value) == "number" then
		local v = value as number
		return fw.LengthValue(v)
	elseif type(value) ~= "nil" then
		print("WARN: Failed to update '" .. key .. "', value of type " .. type(value) .. " is not supported")
	end

	return fw.LengthValue()
end

local function parseBorderColor(style: NativeStyle, key: string, value: fw.Color4FT)
	local col = parseColor4F(value, key)
	style["set_borderLeftColor"](style, col)
	style["set_borderRightColor"](style, col)
	style["set_borderTopColor"](style, col)
	style["set_borderBottomColor"](style, col)
end

local function parseBorderWidth(style: NativeStyle, key: string, value: fw.LengthValueT)
	local len = parseLengthValue(value, key)
	style["set_borderLeftWidth"](style, len)
	style["set_borderRightWidth"](style, len)
	style["set_borderTopWidth"](style, len)
	style["set_borderBottomWidth"](style, len)
end

local function parsePadding(style: NativeStyle, key: string, value: fw.FlexValueT)
	local len = parseFlexValue(value, key)
	style["set_paddingLeft"](style, len)
	style["set_paddingRight"](style, len)
	style["set_paddingTop"](style, len)
	style["set_paddingBottom"](style, len)
end

local function parserWrapper(fn: function(any, string): any): StyleSetterFunc
	return function(style: NativeStyle, key: string, value: any)
		style["set_" .. key](style, fn(value, key))
	end
end

local styleSetters: {string: StyleSetterFunc} = {
	margin = parserWrapper(parseFlexRect),
	position = parserWrapper(parseFlexRect),
	flexBasis = parserWrapper(parseFlexValue),
	minWidth = parserWrapper(parseFlexValue),
	maxWidth = parserWrapper(parseFlexValue),
	minHeight = parserWrapper(parseFlexValue),
	maxHeight = parserWrapper(parseFlexValue),
	width = parserWrapper(parseFlexValue),
	height = parserWrapper(parseFlexValue),
	color = parserWrapper(parseColor4F),
	backgroundColor = parserWrapper(parseColor4F),
	fontSize = parserWrapper(parseLengthValue),

	border = parseBorderWidth,
	borderWidth = parseBorderWidth,
	borderBottomWidth = parserWrapper(parseLengthValue),
	borderTopWidth = parserWrapper(parseLengthValue),
	borderLeftWidth = parserWrapper(parseLengthValue),
	borderRightWidth = parserWrapper(parseLengthValue),

	borderColor = parseBorderColor,
	borderBottomColor = parserWrapper(parseColor4F),
	borderTopColor = parserWrapper(parseColor4F),
	borderLeftColor = parserWrapper(parseColor4F),
	borderRightColor = parserWrapper(parseColor4F),

	marginBottom = parserWrapper(parseFlexValue),
	marginTop = parserWrapper(parseFlexValue),
	marginLeft = parserWrapper(parseFlexValue),
	marginRight = parserWrapper(parseFlexValue),

	padding = parsePadding,
	paddingBottom = parserWrapper(parseFlexValue),
	paddingTop = parserWrapper(parseFlexValue),
	paddingLeft = parserWrapper(parseFlexValue),
	paddingRight = parserWrapper(parseFlexValue)
}

local function parseStyle(node: HostElementHandle, items: {string:any})
	local style = node:getStyle() as NativeStyle
	style:clear()

	for k,v in pairs(items) do
		local value = v
		if value ~= nil then
			local setter = styleSetters[k]
			if setter then
				setter(style, k, value)
			else
				print("Failed to find style setter for " .. k)
			end
		end
	end

	node:updateLayoutStyle()
end

local counter = 0

local reconciler: Reconciler<HostElementHandle, fw.HostStyle, fw.HostEvents> = {
	appendChild = function (node: HostElementHandle, child: HostElementHandle)
		node:addChild(child)
	end,
	removeChild = function (node: HostElementHandle, child: HostElementHandle)
		node:removeChild(child)
	end,
	addEventListener = function (node: HostElementHandle, name: string, func: function(any): any)
		local counterId = node.counterId
		local e = eventBus.entities[counterId]
		if not e then
			e = {}
			eventBus.entities[counterId] = e
		end

		e[name] = func

		fw.document:addEventListener(node, name)
	end,
	removeEventListener = function (node: HostElementHandle, name: string, func: function(any): any)
		local e = eventBus.entities[node.counterId]
		if e then e[name] = nil end

		fw.document:removeEventListener(node, name)
	end,
	getStyle = function (node: HostElementHandle): fw.HostStyle
		return fw.document:getStyle(node)
	end,
	createElement = function (tag: string): HostElementHandle
		local elem = fw.ReactElementView(tag)
		elem.counterId = counter
		counter = counter + 1
		return elem
	end,
	createTextNode = function (text: string): HostElementHandle
		local elem = fw.ReactTextView(text)
		elem.counterId = counter
		counter = counter + 1
		return elem
	end,
	getRootElement = function (): HostElementHandle
		return fw.document
	end,
	setProperty = function(node: HostElementHandle, name: string, value: any)
		assert(type(name) == "string")

		if name == "style" then
			parseStyle(node, value as {string:any})
		elseif name ~= "ref" then
			node[name] = value
		end
	end,
	getProperty = function(node: HostElementHandle, name: string): any
		return node[name]
	end
}

--[[
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
]]
return reconciler
