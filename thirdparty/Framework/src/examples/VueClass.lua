--!strict

require("VueBase")

local CounterView = class("CounterView", View)

function CounterView:init()
	self.state = {
		_value = 1337
	}
end

CounterView:prop("value", 0)

CounterView:prop("value", {
	value = 10,
	min = 10,
	max = 100
})

function CounterView.props.value:get()
	return self._counter
end

function CounterView.props.value:set(value)
	self._counter = value
end

function CounterView:counterText()
	return "The counter is " .. self.counter
end

function CounterView:onInitialize()
	self:addChild(self:createView())
end

function CounterView:createView()
	return View {
		name = "Root",
		sizingPolicy = SizingPolicy.FitToParent,

		views = {
			Text {
				area = Rect.new(100, 10, 100, 150),
				text = self.counterText
			}
		}
	}
end

function onInitialize()
	print("----------------- Initializing")
	local view = CounterView.new()
	uiSelf:addChild(view)
end
