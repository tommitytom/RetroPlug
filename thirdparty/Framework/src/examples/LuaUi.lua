local counter = 0

function onInitialize()
	print("Initializing")

	local label = LabelView.new()

	label.name = "Counter label"
	label.color = Color4F.new(1, 1, 1, 1)
	label.area = Rect.new(150, 25, 100, 100)
	label.text = "This is the starting text!"
	self:addChild(label)

	local button = ButtonView.new()
	button.area = Rect.new(10, 10, 130, 50)
	button.text = "Counter button"
	self:addChild(button)

	local slider = SliderView.new()
	slider.area = Rect.new(10, 70, 200, 30)
	self:addChild(slider)

	self:subscribe(ButtonClickEvent, button, function(ev)
		counter = counter + 1
		print("Counter is now " .. counter)
		label.text = "" .. counter
	end)

	self:subscribe(SliderChangeEvent, slider, function(ev)
		print("slider value: " .. ev.value)
		counter = ev.value
		label.text = "" .. counter
	end)

	--[[local label = LabelView.new({
		name = "Counter label",
		color = Color4F.new(1, 1, 1, 1),
		area = Rect.new(150, 25, 100, 100),
		text = "This is the starting text!"
	})

	local root = View.new({
		name = "Root",
		sizingPolicy = SizingPolicy.FitToParent,
		views = {
			PanelView.new({
				name = "My panel",
				layout = Layouts.grid,
				area = Rect.new(10, 10, 500, 500),
				views = {
					LabelView.new({
						text = "hello world!"
					}),
					LabelView.new({
						text = "hello world!"
					})
				}
			}),
			ButtonView.new({
				area = Rect.new(10, 10, 100, 150),
				text = "My button"
			})
		}
	})]]
end
