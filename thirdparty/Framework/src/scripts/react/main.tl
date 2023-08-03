--global inspect = require("inspect")

local React = require("react")

local r = {
	div = function(props: fw.HostStyle): Element return React.createElement("div", props) end,
	h1 = function(props: fw.HostStyle): Element return React.createElement("h1", props) end,
	span = function(props: fw.HostStyle): Element return React.createElement("span", props) end,
	p = function(props: fw.HostStyle): Element return React.createElement("p", props) end
}

local Counter = React.component(function (props: PropsMap): Element
	local value, setValue = React.useState(0 as integer)

	return r.div {
		backgroundColor = fw.Color4F(0.2, 0.2, 0.2, 1),
		flexDirection = fw.FlexDirection.Row,
		width = 100,
		height = 100,
		onMouseButton = function(ev: fw.MouseButtonEvent)
			if ev.button == 0 and ev.pressed == true then
				setValue(function(current: integer): integer
					return current + 1
				end)
			end
		end,
		onMouseMove = function(ev: fw.MouseMoveEvent)
			print(ev)
			setValue(function(current: integer): integer
				return current + 1
			end)
		end,
		children = {
			"Value: " .. value
		}
	}
end)

local App = React.component(function (props: PropsMap): Element
	return r.div {
		backgroundColor = fw.Color4F(1, 0, 0, 1),
		width = 400,
		height = 400,
		children = {
			"hi",
			Counter {},
			r.div {}
		}
	}
end)

React.render(App {})



global function onFrame(dt: number)
	--print("onFrame", dt)
	local start = fw.getTime()

	React.workLoop(function(): number
		print("loop")
		local diff = fw.getTime() - start
		return (dt - diff) * 1000
	end)
end
