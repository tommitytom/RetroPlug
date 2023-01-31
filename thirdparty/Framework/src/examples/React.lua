--!strict

require("ReactBase")
local fun = require("fun")

local CannonTypes = {
	"Standard",
	"PeaShooter",
	"Hornet",
	"Quedy",
	"Launcher",
	"MulletWill",
	"Pengoo",
	"BubbleGum",
	"CornDog"
}

local CANNON_INDEX_MAX = #CannonTypes - 1

local CannonIcon = Component(function(props, ctx)
	return Image {
		uri = props.filename
	}
end)

local CannonList = Component(function(props, ctx)
	return Container {
		name = "Cannon list",
		sizingPolicy = SizingPolicy.FitToContent,

		components = fun.enumerate(CannonTypes):map(function (idx, name)
			return CannonIcon {
				filename = string.lower(name) .. ".png",
				selected = props.selected == idx,
				locked = false,
				onClick = function() props.onSelectionChanged(idx) end
			}
		end)
	}
end)

local MiniInventory = Component(function(props, ctx)
	local scrollOffset, setScrollOffset = ctx.useState(0)

	local function scroll(amount)
		local offset = clamp(scrollOffset + amount, 0, CANNON_INDEX_MAX)
		setScrollOffset(offset)
	end

	return Container {
		name = "Mini Inventory",
		area = Rect.new(0, 0, 314, 76),

		components = {
			Button {
				name = "Left Arrow",
				area = Rect.new(0, 50, 67, 67),
				onClick = function() scroll(-1) end,
			},
			Container {
				name = "Cannon scroll container",
				area = Rect.new(27, 0, 254, 72),

				components = {
					CannonList {
						left = scrollOffset * 76,
						onSelectionChanged = function(idx) end
					}
				}
			},
			Button {
				name = "Right Arrow",
				area = Rect.new(314 + 17, 50, 67, 67),
				onClick = function() scroll(1) end
			}
		}
	}
end)

function onInitialize()
	print("----------------- Initializing")

	local dom = MiniInventory()
	CreateState(dom, uiSelf)
end
