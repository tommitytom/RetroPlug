local React = require("react")
local r = require("react-elements")

local Counter = React.component(function (_: PropsMap): Element
	local value, setValue = React.useState(0 as integer)

	return r.button {
		text = tostring(value),

		onClick = function(_: fw.MouseButtonEvent)
			setValue(function(current: integer): integer
				return current + 1
			end)
		end
	}
end)

local ColorChanger = React.component(function (_: PropsMap): Element
	local value, setValue = React.useState(fw.Color4F(0, 0, 0, 1))

	return r.div {
		backgroundColor = value,
		width = 100,
		height = 100,

		onMouseMove = function(ev: fw.MouseMoveEvent)
			setValue(fw.Color4F(ev.position.x / 100, 0, 0, 1))
		end,
	}
end)

local SplitPanel = React.component(function (_: PropsMap): Element
	local value, setValue = React.useState(fw.Color4F(0, 0, 0, 1))

	return r.div {
		backgroundColor = value,
		width = 100,
		height = 100,

		onMouseMove = function(ev: fw.MouseMoveEvent)
			setValue(fw.Color4F(ev.position.x / 100, 0, 0, 1))
		end,
	}
end)

local App = React.component(function (_: PropsMap): Element
	local value, setValue = React.useState("hi")
	local count, setCount = React.useState(0 as integer)

	return r.div {
		flexDirection = fw.FlexDirection.Row,
		backgroundColor = fw.Color4F(1, 0, 0, 1),
		width = fw.FlexValue(fw.FlexUnit.Percent, 100),
		height = fw.FlexValue(fw.FlexUnit.Percent, 100),
		children = {
			r.div {
				backgroundColor = fw.Color4F(0, 1, 0, 1),
				width = fw.FlexValue(fw.FlexUnit.Percent, 25),
				height = fw.FlexValue(fw.FlexUnit.Percent, 100),
				children = {
					r.button { text = tostring(count) },
					r.button {
						text ="shiz",
						onClick = function(_: fw.MouseButtonEvent)
							setCount(count + 1)
						end
					},
					r.div {
						width = 100,
						height = 100,
						className = "containerl"
					}
				}
			},
			r.div {
				color = fw.Color4F(1, 1, 1, 1),
				backgroundColor = fw.Color4F(0, 0, 0, 0.0),
				width = 11,
				margin = fw.FlexRect(0, -5, 0, -5),
				border = fw.FlexBorder(0, 5, 0, 5),
				cursor = fw.CursorType.ResizeEW,
				--draggable = true,
				onMouseMove = function(ev: fw.MouseMoveEvent)
					--setCursor(fw.CursorType.ResizeEW)
				end,
			},
			r.div {
				backgroundColor = fw.Color4F(0, 0, 1, 1),
				width = fw.FlexValue(fw.FlexUnit.Percent, 75),
				height = fw.FlexValue(fw.FlexUnit.Percent, 100),
			}
		}
	}
end)

local Basic = React.component(function (_: PropsMap): Element
	return r.div {
		children = {
			"Hi"
		}
		--className = "containerl"
	}
end)

React.render(Basic {})
