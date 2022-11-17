require("View")

local function ref(v) end
local function fmt(s, ...) end
local function conditional(value, tr, fa) end

local counter = 0
local offset = 0
local achievements = ref(0)
local showValue = ref(true)
local tex = ref("up.png")

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
				text = "My button",
				[ButtonClickEvent] = function() print("button clicked!") end
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
		}
	})
end

function onInitialize()
	print("----------------- Initializing")
	self:addChild(createView())
end

function onRender(canvas)
	canvas:fillRect(RectF.new(10, 400, 100, 100), Color4F.new(1, 1, 0, 1))
	canvas:fillRect(RectF.new(10 + offset, 400, 100, 100), Color4F.new(1, 0, 1, 1))
end
