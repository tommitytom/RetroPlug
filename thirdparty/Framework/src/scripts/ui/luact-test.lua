local root = ";E:\\code\\RetroPlugNext\\thirdparty\\Framework\\src\\scripts\\ui\\"
package.path = package.path .. root .. "?.lua"
package.path = package.path .. root .. "?\\init.lua"

inspect = require "inspect"

local ReactApp = require("luact-fw")
local React = require "luact-fw.reconciler"
local events = require "luact-fw.events"
local luact = require "luact"
local frame = require "luact.timers.frame"
--local logs = require "luact.utils.logs"

local document = fw.registry

local function updateScalarValue(value, style, key)
	if value ~= nil then
		style[key] = value
	else
		style[key] = 0
	end
end

local function updateFlexBorder(value, style, key)
	if type(value) == "number" then
		style[key] = fw.FlexBorder(value, value, value, value)
	elseif type(value) == "table" then
		style[key] = fw.FlexBorder(value.left, value.top, value.right, value.bottom)
	elseif type(value) == "cdata" then
		style[key] = value
	elseif type(value) == "nil" then
		style[key] = fw.FlexBorder()
	else
		style[key] = fw.FlexBorder()
		print("WARN: Failed to update '" .. key .. "', value of type " .. type(value) .. " is not supported")
	end
end

local function updateFlexRect(value, style, key)
	if type(value) == "number" then
		local v = fw.FlexValue(value)
		style[key] = fw.FlexRect(v, v, v, v)
	elseif type(value) == "table" then
		style[key] = fw.FlexRect(value.left, value.top, value.right, value.bottom)
	elseif type(value) == "userdata" then
		style[key] = value
	elseif type(value) == "nil" then
		style[key] = fw.FlexRect()
	else
		style[key] = fw.FlexRect()
		print("WARN: Failed to update '" .. key .. "', value of type " .. type(value) .. " is not supported")
	end
end

local function updateFlexValue(value, style, key)
	if type(value) == "number" then
		style[key] = fw.FlexValue(value)
	elseif type(value) == "userdata" then
		style[key] = value
	elseif type(value) == "nil" then
		style[key] = fw.FlexValue()
	else
		style[key] = fw.FlexValue()
		print("WARN: Failed to update '" .. key .. "', value of type " .. type(value) .. " is not supported")
	end
end

local styleUpdaters = {
	border = updateFlexBorder,
	padding = updateFlexRect,
	margin = updateFlexRect,
	position = updateFlexRect,
	flexDirection = updateScalarValue,
	justifyContent = updateScalarValue,
	flexAlignItems = updateScalarValue,
	flexAlignSelf = updateScalarValue,
	flexAlignContent = updateScalarValue,
	layoutDirection = updateScalarValue,
	flexWrap = updateScalarValue,
	flexGrow = updateScalarValue,
	flexShrink = updateScalarValue,
	flexBasis = updateFlexValue,
	minWidth = updateFlexValue,
	maxWidth = updateFlexValue,
	minHeight = updateFlexValue,
	maxHeight = updateFlexValue,
	width = updateFlexValue,
	height = updateFlexValue,
	aspectRatio = updateScalarValue,
	overflow = updateScalarValue
}

local componentUpdaters = {
	color = true,
	backgroundColor = true,
}

local function updateStyleFields(oldProps, newProps, style)
	for key, updater in pairs(styleUpdaters) do
		local np = newProps[key]

		if np ~= oldProps[key] then
			updater(np, style, key)
		end
	end
end

local function updateComponentFields(oldProps, newProps, entity)
	for key, _ in pairs(componentUpdaters) do
		local np = newProps[key]

		if np ~= oldProps[key] then
			document["set_" .. key](document, entity, np)
		end
	end
end

local eventUpdaters = {
	onMouseMove = true,
	onMouseButton = true,
	onMouseEnter = true,
	onMouseLeave = true
}

local function updateEvents(oldProps, newProps, entity)
	for key, _ in pairs(eventUpdaters) do
		local np = newProps[key]
		local op = oldProps[key]

		if np ~= op then
			if op then
				events.remove(entity, key)
			end

			if np then
				events.add(entity, key, np)
			end
		end
	end
end

local function divCommit(entity, oldProps, newProps)
	updateStyleFields(oldProps, newProps, document:getNodeStyle(entity))
	updateComponentFields(oldProps, newProps, entity)
	updateEvents(oldProps, newProps, entity)
end

local function divCreate(entity, props)
	divCommit(entity, {}, props)
end

local divMethods = {
	create = divCreate,
	commit = divCommit
}

local function Div(props)
	return React.Element(divMethods, props)
end

local function textCreate(entity, props)
	divCreate(entity, props)
	document:setNodeText(entity, props.text)
end

local function textCommit(entity, oldProps, newProps)
	divCommit(entity, oldProps, newProps)
	document:setNodeText(entity, newProps.text)
end

local textMethods = {
	create = textCreate,
	commit = textCommit
}

local function Text(props)
	return React.Element(textMethods, props)
end

local Button = React.component(function(props)
	local mouseOver, setMouseOver = luact.use_state(0)
	local buttonText, setButtonText = luact.use_state("Button!")

	local backgroundColor = mouseOver and fw.Color4F(0.6, 0.6, 0.6, 1) or fw.Color4F(0.4, 0.4, 0.4, 1)

	return Div {
		border = 2,
		onMouseEnter = function() setMouseOver(function() return true end) end,
		onMouseLeave = function() setMouseOver(function() return false end) end,
		--onMouseMove = function(ev) setButtonText(function() return "Button! " .. ev.position.x .. ", " .. ev.position.y end) end,
		onMouseMove = function(ev) setButtonText("Button! " .. ev.position.x .. ", " .. ev.position.y) end,
		onMouseButton = function(ev) print(ev.position) end,
		children = {
			Text {
				text = buttonText,
				backgroundColor = fw.BackgroundComponent(backgroundColor),
				padding = 20
			}
		}
	}
end)

local App = React.component(function(props)
	local count, set_count = luact.use_state(0)

	--[[luact.use_layout_effect(function ()
		local request

		local function animate(dt)
			set_count(function (current)
				return current + 1
			end)

		  	request = frame.request(animate)
		end

		request = frame.request(animate)

		return function ()
		  frame.clear(request)
		end
	end, {})]]

	return Div {
		flexDirection = fw.FlexDirection.Row,
		width = fw.FlexValue(fw.FlexUnit.Percent, 50),
		height = fw.FlexValue(400),
		backgroundColor = fw.BackgroundComponent(fw.Color4F(1, 0, 0, 1)),
		children = {
			Div {
				flexDirection = fw.FlexDirection.Colum,
				backgroundColor = fw.BackgroundComponent(fw.Color4F(1, 1, 1, 1)),
				width = 100,
				height = 100,
				children = {
					Div {
						backgroundColor = fw.BackgroundComponent(fw.Color4F(0.5, 0.5, 0.5, 1)),
						width = 50,
						height = 50
					},
					Div {
						backgroundColor = fw.BackgroundComponent(fw.Color4F(0.6, 0.6, 0.6, 1)),
						width = 50,
						height = 50
					}
				}
			},
			Text {
				backgroundColor = fw.BackgroundComponent(fw.Color4F(0, 1, 0, 1)),
				width = 100,
				height = 100,
				text = "Counttt: " .. count
			},
			Button { text = "Button!" },
			Text {
				backgroundColor = fw.BackgroundComponent(fw.Color4F(0, 1, 0, 1)),
				width = 100,
				height = 100,
				text = "Counttt: " .. count
			}
		}
	}
end)

local Main = React.create_meta(function (class)
	function class:component_did_catch(errors)
		--error(logs(errors, 0))
		error(errors)
	end

	function class:render()
		return React.Fragment {
			children = {
		  		App {},
				App {}
			}
	  	}
	end
end)

ReactApp.init(Main {})

function processHostEvent(entity, name, ev)
	local emitter = events.find(entity, name)
	if emitter then
		emitter(ev)
		return true
	end

	return false
end

function onFrame(dt)
	--print("onFrame", dt)
	local start = fw.getTime()
	luact.update_frame(dt)

	React.work_loop(function ()
		--print("loop")
		local diff = fw.getTime() - start
		return (dt - diff) * 1000
	end)
end
