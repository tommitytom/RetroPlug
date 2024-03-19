local React = require("react")
local r = require("react-elements")

fw.loadStyle("E:\\code\\RetroPlugNext\\thirdparty\\Framework\\src\\scripts\\react\\retroplug.css")

local record MenuRowProps
	selected: boolean
	indent: integer
	children: {Element|string}
end
local MenuRow = React.component(function (props: MenuRowProps): Element
	local className = "menuRow"
	if props.selected == true then className = className .. " selected" end

	return r.div {
		className = className,
		style = {
			paddingLeft = fw.FlexValue(20 + props.indent * 20),
			paddingRight = fw.FlexValue(10)
		},
		children = props.children
	}
end)

local record MenuSelectorProps
	selected: string
end
local MenuSelector = React.component(function (props: MenuSelectorProps): Element
	return r.div {
		className = "multiselect",
		r.span { style = { paddingLeft = fw.FlexValue(20) }, "<" },
		r.span { className = "multiselect-text", props.selected },
		r.span { ">" },
	}
end)

local record MenuCheckboxProps
	checked: boolean
end
local MenuCheckbox = React.component(function (props: MenuCheckboxProps): Element
	return r.div {
		className = "checkbox",
		r.span {
			className = "multiselect-text",
		},
		r.div {
			className = "checkbox-check",
			style = {
				fontSize = 15,
				width = 20,
				borderLeftWidth = fw.LengthValue(1),
				borderRightWidth = fw.LengthValue(1),
				borderTopWidth = fw.LengthValue(1),
				borderBottomWidth = fw.LengthValue(1),
				borderLeftColor = fw.Color4F(1, 1, 1, 1),
			},
			"X"
		}
	}
end)

local record MenuItemType
	name: string
	type: string
	items: {string}
	children: {MenuItemType}
end

local menuItems: MenuItemType = {
	children = {
		{
			name = "Load",
			children = {
				{ name = "ROM..." },
				{
					name = "ROM As",
					children = {
						{ name = "ABG..." },
						{ name = "CBG C..." },
						{ name = "DMG..." },
					}
				}
			},
		},
		{
			name = "stuff",
			children = {
				{
					name = "Select",
					type = "multiselect",
					items = { "a", "b", "c" }
				},
				{
					name = "Check",
					type = "checkbox",
				},
			}
		},
		{
			name = "Add System",
			children = {
				{ name = "Duplicate current" },
				{ name = "ROM..." },
				{
					name = "ROM As",
					children = {
						{ name = "ABG..." },
						{ name = "CBG C..." },
						{ name = "DMG..." },
					}
				}
			}
		}, {
			name = "Load MGB"
		}
	}
}

local function foreach<T, R>(tab: {T}, fn: function(T): R): {R}
	local items = {}
	for _,v in ipairs(tab) do
		table.insert(items, fn(v))
	end

	return items
end

local record MenuIdCounter
	count: integer
end

local function nextId(counter: MenuIdCounter): integer
	local value = counter.count
	counter.count = counter.count + 1
	return value
end

local function renderMenu(item: MenuItemType, idx: MenuIdCounter, selectedIdx: number, indent: integer, items: {Element})
	local id = nextId(idx)
	local selected = id == selectedIdx
	local content: Element = nil

	if item.type == "multiselect" then
		content = MenuSelector { selected = "itemA" }
	elseif item.type == "checkbox" then
		content = MenuCheckbox { checked = true }
	end

	table.insert(items, MenuRow{
		indent = indent,
		selected = selected,
		item.name,
		content
	})

	if item.children ~= nil then
		for _, v in ipairs(item.children) do
			renderMenu(v, idx, selectedIdx, indent + 1, items)
		end
	end
end

local Basic = React.component(function (_: PropsMap): Element
	local counter: MenuIdCounter = { count  = 0 }
	local pos, setPos = React.useState(0 as integer)
	local refTest = React.useRef(nil as fw.HostElement)

	local renderedItems = {}
	for _, v in ipairs(menuItems.children) do
		renderMenu(v, counter, pos, 0, renderedItems)
	end

	return r.div {
		className = "root",
		ref = refTest,

		tabIndex = 0,
		onKey = function(ev: fw.KeyEvent): boolean
			if ev.key == fw.VirtualKey.UpArrow and ev.down == true and pos > 0 then
				setPos(pos - 1)
			end

			if ev.key == fw.VirtualKey.DownArrow and ev.down == true then
				setPos(pos + 1)
			end
		end,

		r.div {
			style = {
				backgroundColor = fw.Color4F(0.2, 0.5, 0.2, 1),
				width = 160 * 3,
				height = 144 * 3,

				borderLeftWidth = fw.LengthValue(1),
				borderRightWidth = fw.LengthValue(1),
				borderTopWidth = fw.LengthValue(1),
				borderBottomWidth = fw.LengthValue(1),
				borderLeftColor = fw.Color4F(1, 0, 0, 1)
			},
		},
		r.div {
			style = {
				backgroundColor = fw.Color4F(1, 1, 0, 1),
				width = 160 * 3,
				height = 144 * 3
			},
			r.div {
				className = "overlay",

				onMouseButton = function(ev: fw.MouseButtonEvent): boolean
					print("click", ev.button)
					return true
				end,

				r.span {
					style = {
						marginTop = fw.FlexValue(20),
						marginLeft = fw.FlexValue(20),
						fontSize = 30
					},
					"RetroPlug 0.4.0",
				},
				r.div {
					style = {
						width = "100%",
						height = 2,
						backgroundColor = fw.Color4F(1,1,1,1),
						marginTop = fw.FlexValue(20),
						marginBottom = fw.FlexValue(20),
					}
				},
				table.unpack(renderedItems)
			}
		},
	}
end)

React.render(Basic {})
