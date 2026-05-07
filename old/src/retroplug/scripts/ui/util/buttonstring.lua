local util = require("util")
local pathutil = require("pathutil")

local module = {}

local function writeButtons(str, target, buttons)
	str = str .. "("

	local first = true
	for _, v in ipairs(buttons) do
		if not first then
			str = str .. "/"
		else
			first = false
		end

		local name = util.enumToString(Key, target[v])
		if util.startsWith(name, "Num") and not util.startsWith(name, "NumPad") then
			name = name:sub(4)
		end

		str = str .. name
	end

	str = str .. ")"

	return str
end

function module.formatKeymapName(filename, keymap)
	local name = pathutil.removeExt(filename)
	local config = keymap.config

	if config then
		if config.name ~= nil and config.name ~= "" then
			name = config.name
		end
	end

	local target = {}
	for _, km in ipairs(keymap.key) do
		for k, v in pairs(km.lookup) do
			if type(k) == "number" and type(v) == "number" then
				target[v] = k
			end
		end
	end

	local buttonOutput = " - "

	if 	target[Button.Left] == Key.LeftArrow and target[Button.Right] == Key.RightArrow and
		target[Button.Up] == Key.UpArrow and target[Button.Down] == Key.DownArrow
	then
		buttonOutput = buttonOutput .. "(Arrows) "
	else
		buttonOutput = writeButtons(buttonOutput, target, { Button.Up, Button.Left, Button.Down, Button.Right }) .. " "
	end

	buttonOutput = writeButtons(buttonOutput, target, { Button.Select, Button.Start }) .. " "
	buttonOutput = writeButtons(buttonOutput, target, { Button.B, Button.A })

	return name .. buttonOutput
end

return module
