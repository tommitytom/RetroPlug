local React = require("react")

local r = {
	div = function(props: fw.HostStyle): Element return React.createElement("div", props) end,
	h1 = function(props: fw.HostStyle): Element return React.createElement("h1", props) end,
	span = function(props: fw.HostStyle): Element return React.createElement("span", props) end,
	p = function(props: fw.HostStyle): Element return React.createElement("p", props) end
}

local record ButtonProps
	text: string
	onClick: function(fw.MouseButtonEvent)
end

local button = React.component(function (props: ButtonProps): Element
	local hover, setHover = React.useState(false)
	local activated, setActivated = React.useState(false)

	local col: fw.Color4FT
	if activated == true then
		col = fw.Color4F(0.5, 0.5, 0.5, 1)
	elseif hover == true then
		col = fw.Color4F(0.3, 0.3, 0.3, 1)
	else
		col = fw.Color4F(0.2, 0.2, 0.2, 1)
	end

	return r.div {
		backgroundColor = col,
		padding = 20,
		border = 1,

		onMouseEnter = function(_: fw.MouseEnterEvent)
			setHover(true)
		end,
		onMouseLeave = function()
			setHover(false)
		end,
		onMouseFocus = function()
			setActivated(true)
		end,
		onMouseBlur = function()
			setActivated(false)
		end,
		onMouseButton = function(ev: fw.MouseButtonEvent)
			if ev.button == fw.MouseButton.Left then
				if activated == true and ev.down == false and props.onClick then
					props.onClick(ev)
				end
			end
		end,

		children = {
			(props.text as string) or ""
		}
	}
end)

return {
	div = r.div,
	h1 = r.h1,
	span = r.span,
	p = r.p,

	button = button
}
