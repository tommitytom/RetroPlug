actions({ name = "Lsdj", rom = "lsdj*" }, {
	DownTenRows = function()
		system.buttons:hold(Buttons.B):delay():hold(Buttons.Down):releaseAll()
	end,
	UpTenRows = function()
		system.buttons:hold(Buttons.B):hold(Buttons.Up):releaseAll()
	end,
	ScreenUp = function()
		system.buttons:hold(Buttons.Select):hold(Buttons.Up):releaseAll()
	end,
	ScreenDown = function()
		system.buttons:hold(Buttons.Select):hold(Buttons.Down):releaseAll()
	end,
	ScreenLeft = function()
		system.buttons:hold(Buttons.Select):hold(Buttons.Left):releaseAll()
	end,
	ScreenRight = function()
		system.buttons:hold(Buttons.Select):hold(Buttons.Right):releaseAll()
	end,
	Delete = function()
		system.buttons:hold(Buttons.B):hold(Buttons.A):releaseAll()
	end,
	BeginSelection = function()
		system.buttons:hold(Buttons.Select):hold(Buttons.B):releaseAll()
	end,
	BeginRowSelection = function()
		system.buttons:hold(Buttons.Select):press(Buttons.B):hold(Buttons.B):releaseAll()
	end,
	SelectAll = function()
		system.buttons:hold(Buttons.Select):press(Buttons.B):press(Buttons.B):hold(Buttons.B):releaseAll()
	end,
	CancelSelection = function()
		system.buttons:press(Buttons.B)
	end,
	Copy = function()
		system.buttons:press(Buttons.B)
	end,
	Cut = function()
		system.buttons:hold(Buttons.Select):hold(Buttons.A):releaseAll()
	end,
	Paste = function()
		system.buttons:hold(Buttons.Select):hold(Buttons.A):releaseAll()
	end,
})

local DEFAULT_BUTTON_DELAY = 50

ButtonStream = class()
function ButtonStream:init()
	self.state = { false, false, false, false, false, false, false, false }
	self.queue = {}
	self.buffer = {}
end

function ButtonStream:hold(button)
	-- Only process this button press if it is not already down
	if self.state[button] == false then
		local queueItem = { delay = DEFAULT_BUTTON_DELAY }
		if #self.queue == 0 then
			-- We don't have to wait for any keypresses to finish so process immediately
			self.buffer.hold(button)
			self.state[button] = true
		else
			queueItem.button = button
			queueItem.down = true
		end

		table.insert(self.queue, queueItem)
	end
end

function ButtonStream:process(delta)
	while #self.queue > 0 and delta > 0 do
		local queueItem = table.remove(self.queue, 1)
	end

	for i,v in ipairs(self.queue) do
		if delta >= v.delay then
			delta = delta - v.delay
		elseif v.button ~= nil then

		else
			break
		end
	end
end