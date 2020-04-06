local class = require("class")

local SelectState = {
	None = 0,
	RequestBegin = 1,
	Selecting = 2,
	Selected = 3
}

local function beginSelect(buttons)
	return buttons:hold(Button.Select):hold(Button.B):releaseAll()
end

local function endSelect(buttons)
	return buttons:press(Button.B)
end

local function isDirectionButton(button)
	return button == Button.Left or button == Button.Right or button == Button.Up or button == Button.Down
end

local KeyboardActions = class()
function KeyboardActions:init(system)
    self._system = system
    self._buttons = system:buttons()
    self._selectState = SelectState.None
end

function KeyboardActions:downTenRows(down)
    if down == true then self._buttons:releaseAll():hold(Button.B):hold(Button.Down):releaseAll() end
end

function KeyboardActions:upTenRows(down)
    if down == true then self._buttons:releaseAll():hold(Button.B):hold(Button.Up):releaseAll() end
end

function KeyboardActions:screenUp(down)
    if down == true then self._buttons:releaseAll():hold(Button.Select):hold(Button.Up):releaseAll() end
end

function KeyboardActions:screenDown(down)
    if down == true then self._buttons:releaseAll():hold(Button.Select):hold(Button.Down):releaseAll() end
end

function KeyboardActions:screenLeft(down)
    if down == true then self._buttons:releaseAll():hold(Button.Select):hold(Button.Left):releaseAll() end
end

function KeyboardActions:screenRight(down)
    if down == true then self._buttons:releaseAll():hold(Button.Select):hold(Button.Right):releaseAll() end
end

function KeyboardActions:delete(down)
    if down == true then
        if self._selectState == SelectState.None then
            self._buttons:releaseAll():hold(Button.B):hold(Button.A):releaseAll()
        elseif self._selectState == SelectState.Selecting or self._selectState == SelectState.Selected then
            self._buttons:hold(Button.Select):hold(Button.A):releaseAll()
            self._selectState = SelectState.None
        end
    end
end

function KeyboardActions:beginSelection(down)
    if down == true then
        if self._selectState == SelectState.None then
            self._selectState = SelectState.RequestBegin
            self._buttons:hold(Button.Select)
        elseif self._selectState == SelectState.Selected then
            self._selectState = SelectState.Selecting
        end
    else
        if self._selectState == SelectState.Selecting then
            self._selectState = SelectState.Selected
        elseif self._selectState == SelectState.RequestBegin then
            self._selectState = SelectState.None
            self._buttons:release(Button.Select)
        end
    end
end

function KeyboardActions:cancelSelection(down)
    if down == true then
        if self._selectState == SelectState.Selecting or self._selectState == SelectState.Selected then
            endSelect(self._buttons)
            self._selectState = SelectState.None
        end
    end
end

function KeyboardActions:copy(down)
    if down == true then
        if self._selectState == SelectState.None or self._selectState == SelectState.RequestBegin then
            beginSelect(self._buttons)
        end

        self._buttons:releaseAll():press(Button.B)
        self._selectState = SelectState.None
    end
end

function KeyboardActions:cut(down)
    if down == true then
        if self._selectState == SelectState.None or self._selectState == SelectState.RequestBegin then
            beginSelect(self._buttons)
        end

        self._buttons:releaseAll():hold(Button.Select):hold(Button.A):releaseAll()
        self._selectState = SelectState.None
    end
end

function KeyboardActions:paste(down)
    if down == true then self._buttons:hold(Button.Select):hold(Button.A):releaseAll() end
end

function KeyboardActions:_handleButtonPress(button, down)
    if self._selectState == SelectState.RequestBegin then
		if down == true then
			if isDirectionButton(button) == true then
				beginSelect(self._buttons):hold(button)
				self._selectState = SelectState.Selecting
				return false
			end
		end
	elseif self._selectState == SelectState.Selected then
		if down == true then
			if isDirectionButton(button) == true then
				endSelect(self._buttons):hold(button)
				self._selectState = SelectState.None
				return false
			else
			end
		end
	end
end
