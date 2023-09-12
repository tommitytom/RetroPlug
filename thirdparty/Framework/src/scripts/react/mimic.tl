local React = require("react")
local r = require("react-elements")

local record BorderedRectProps
	name: string
end

local BorderedRect = React.component(function (props: BorderedRectProps): Element
	local function mouseEnter()
		print("onMouseEnter: " .. props.name)
	end

	local function mouseLeave()
		print("onMouseEnter: " .. props.name)
	end

	return r.div {
		flexDirection = fw.FlexDirection.Column,
		backgroundColor = fw.Color4F(1, 0, 0, 1),
		width = 120,
		height = 120,
		color = fw.Color4F(1, 1, 1, 0.5),
		border = 10,
		onMouseEnter = function ()
			print("onMouseEnter: " .. props.name)
		end,
		onMouseLeave = function ()
			print("onMouseLeave: " .. props.name)
		end,
		onMouseMove = function (ev: fw.MouseMoveEvent)
			print("onMouseMove: " .. props.name .. " - " .. ev.position.x .. ", " .. ev.position.y)
		end,
		children = {
			r.div {
				width = 50,
				height = 50,
				backgroundColor = fw.Color4F(0, 0, 1, 1),
				onMouseEnter = function ()
					print("onMouseEnter: inner " .. props.name)
				end,
				onMouseLeave = function ()
					print("onMouseLeave: inner" .. props.name)
				end,
				onMouseMove = function (ev: fw.MouseMoveEvent)
					print("onMouseMove: inner " .. props.name .. " - " .. ev.position.x .. ", " .. ev.position.y)
				end,
			}
		}
	}
end)

local App = React.component(function (_: PropsMap): Element
	return r.div {
		children = {
			BorderedRect { name = "rect1" },
			BorderedRect { name = "rect2" }
		}
	}
end)

React.render(App {})
