require("ReactiveView")

local counter = 0
local offset = 0
local achievements = ref(0)
local showValue = ref(true)
local tex = ref("up.png")

local Image = TextureView.new
local Button = ButtonView.new

local function createView2()
	return Image {
		name = "Root",
		sizingPolicy = SizingPolicy.FitToParent,
		uri = fmt("../../resources/textures/{}", tex),

		views = {
			Button {
				area = Rect.new(100, 10, 100, 150),
				text = tex,

				[ButtonClickEvent] = function()
					print("button clicked111!")
					counter = counter + 1
					tex:set("value: " .. counter)
				end
			}
		}
	}
end

local function createView()
	return TextureView.new({
		name = "Root",
		sizingPolicy = SizingPolicy.FitToParent,
		--uri = fmt("../../resources/textures/{}", tex),
		uri = "../../resources/textures/up.png",

		views = {
			TextureView.new({
				name = "My panel",
				--layout = Layouts.grid,
				--color = Color4F.new(1, 0, 0, 1),
				--sizingPolicy = SizingPolicy.FitToParent,

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
			--[[ListView.new({
				model = achievements,
				slot = function(achievement)
					return LabelView.new({
						text = achievement.name
					})
				end
			}),]]
			ButtonView.new({
				area = Rect.new(100, 10, 100, 150),
				text = tex,
				[ButtonClickEvent] = function()
					print("button clicked111!")
					counter = counter + 1
					tex:set("value: " .. counter)
				end
			}),
			conditional(showValue,
				function()
					return LabelView.new({
						text = "Showing the value"
					})
				end,
				function()
					return LabelView.new({
						text = "Not showing the value"
					})
				end
			),
			conditional(showValue,
				LabelView.new({
					text = "Showing the value"
				}),
				LabelView.new({
					text = "Not showing the value"
				})
			),
		}
	})
end

function onInitialize()
	print("----------------- Initializing")
	uiSelf:addChild(createView())
end
