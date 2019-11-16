require("constants")

local KeyMap = {
	[Key.RIGHT] = Button.Right,
	[Key.LEFT] = Button.Left,
	[Key.UP] = Button.Up,
	[Key.DOWN] = Button.Down,
}

function onKey(key, down)
	local button = KeyMap[key.vk]
	if button ~= nil then
		print("KEY!", key.vk, down, button)
		active:setButtonState(button, down)
	end
end