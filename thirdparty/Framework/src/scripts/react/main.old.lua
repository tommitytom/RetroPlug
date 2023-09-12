local root = ";E:\\code\\RetroPlugNext\\thirdparty\\Framework\\src\\scripts\\ui\\react\\"
package.path = package.path .. root .. "?.lua"
package.path = package.path .. root .. "?\\init.lua"

inspect = require("inspect")

local eventBus = {

}

local reconciler = {
	appendChild = function (node, child) fw.document.appendChild(node, child) end,
	removeChild = function (node, child) fw.document.removeChild(node, child) end,
	addEventListener = function (node, name, func)
		fw.document.addEventListener(node, child)
	end,
	removeEventListener = function (node, name, func)
		fw.document.removeEventListener(node, child)
	end,
	getStyle = function (node) return fw.document.getStyle(node) end,
	createElement = function (tag) return fw.document.createElement(tag) end,
	createTextNode = function (text) return fw.document.createTextNode(text) end,
	getRootElement = function () return fw.document.getRootElement() end
}

local React = require("react")(reconciler)

local r = {
	div = function(props) return React.createElement("div", props) end,
	h1 = function(props) return React.createElement("h1", props) end,
	span = function(props) return React.createElement("span", props) end,
	p = function(props) return React.createElement("p", props) end
}

local Counter = React.component(function (props)
	local value, setValue = React.useState(0)

	return r.div {
		flexWrap = value,
		onMouseMove = function()
			print("hi")
			setValue(function(current) return current + 1 end)
		end,
		children = {
			r.div {}
		}
	}
end)

local App = React.component(function (props)
	local value, setValue = React.useState(0)

	return r.div {
		flexWrap = value,
		onMouseMove = function()
			setValue(function(current) return current + 1 end)
		end,
		children = {
			"hi",
			r.div {}
		}
	}
end)

React.render(App {})

function processHostEvent(entity, name, ev)
	--[[local emitter = events.find(entity, name)
	if emitter then
		emitter(ev)
		return true
	end]]

	return false
end

function onFrame(dt)
	--print("onFrame", dt)
	local start = fw.getTime()

	React.workLoop(function ()
		--print("loop")
		local diff = fw.getTime() - start
		return (dt - diff) * 1000
	end)
end
