local React = require("react")
local r = require("react-elements")

local ColorChanger = React.component(function (_: PropsMap): Element
	local value, setValue = React.useState(fw.Color4F(0, 0, 0, 1))

	return r.div {
		style = {
			backgroundColor = value
		},
		onMouseMove = function(ev: fw.MouseMoveEvent)
			setValue(fw.Color4F(ev.position.x / 100, 0, 0, 1))
		end,
	}
end)

local App = React.component(function (_: PropsMap): Element
	local value, setValue = React.useState("hi")
	local count, setCount = React.useState(0 as integer)

	return r.div {
		className = "container",

		r.div {
			className = "container-l",

			r.button { tostring(count) },
			r.button {
				className = "fancy",
				onMouseButton = function(ev: fw.MouseButtonEvent)
					if ev.down == true then
						setCount(count + 1)
					end
				end,
				onMouseEnter = function(ev: fw.MouseEnterEvent)
					print(ev.position.x, ev.position.y)
				end,
				onMouseMove = function(ev: fw.MouseMoveEvent)
					print("weee")
					--setCursor(fw.CursorType.ResizeEW)
				end,
				"shiz"
			},
			--tostring(count)
		},
		r.div {
			className = "container-slider",
			--draggable = true,
			onMouseMove = function(ev: fw.MouseMoveEvent)
				print("weee")
				--setCursor(fw.CursorType.ResizeEW)
			end,
		},
		r.div {
			className = "container-r",
			onMouseMove = function(ev: fw.MouseMoveEvent)
				print("weee")
				--setCursor(fw.CursorType.ResizeEW)
			end,
			"I'm just gonna write some text here i think..."
		}
	}
end)

local Basic = React.component(function (_: PropsMap): Element
	local count, setCount = React.useState(0 as integer)

	return r.div {
		r.button {
			className = "fancy",
			onMouseButton = function(ev: fw.MouseButtonEvent)
				setCount(count + 1)
			end,
			"This is a button!",
		},
		r.button {
			className = "fancy",
			onMouseButton = function(ev: fw.MouseButtonEvent)
				setCount(count + 1)
			end,
			"This is also a button!",
		},
		tostring(count)
	}
end)

React.render(App {})
