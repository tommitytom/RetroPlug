local React = require("react")
local r = require("react-elements")
local menuItems = require("testmenu")

fw.loadStyle("C:\\projects\\code\\RetroPlug\\thirdparty\\orb\\src\\scripts\\react\\retroplug.css")

local record MenuRowProps
	idx: integer
	selectedIdx: integer
	setSelected: function(integer)
	onClick: function()
	indent: integer
	children: {Element|string}
end
local MenuRow = React.component(function (props: MenuRowProps): Element
	local className = "menuRow"
	if props.selectedIdx == props.idx then className = className .. " selected" end

	return r.div {
		className = className,
		style = {
			paddingLeft = 20 + props.indent * 20
		},
		onMouseMove = function()
			if props.idx ~= props.selectedIdx then
				props.setSelected(props.idx)
			end
		end,
		onMouseButton = function(ev: fw.MouseButtonEvent)
			if props.onClick and ev.button == fw.MouseButton.Left and ev.down == true then
				props.onClick()
			end
		end,
		children = props.children
	}
end)

local record MenuSelectorProps
	selected: string
end
local MenuSelector = React.component(function (props: MenuSelectorProps): Element
	return r.div {
		className = "multiselect",
		r.span { className = "multiselect-scroller", "<" },
		r.span { className = "multiselect-text", props.selected },
		r.span { className = "multiselect-scroller", ">" }
	}
end)

local record MenuCheckboxProps
	checked: boolean
end
local MenuCheckbox = React.component(function (props: MenuCheckboxProps): Element
	local content: string = nil
	if props.checked then content = "X" end

	return r.div {
		className = "checkbox",
		r.div { className = "checkbox-check", content }
	}
end)

local record MenuIdCounter
	count: integer
end

local function nextId(counter: MenuIdCounter): integer
	local value = counter.count
	counter.count = counter.count + 1
	return value
end

local type ExpandState = {string: boolean}

local record MenuProps
	menu: MenuItemType
end
local Menu = React.component(function (props: MenuProps): Element
	local counter: MenuIdCounter = { count = 0 }
	local expandState, setExpandState = React.useState({} as ExpandState)
	local selectedId, setSelectedId = React.useState(0 as integer)
	local setSelected = setSelectedId as function(integer)
	local items: {Element} = {}

	local function renderMenu(item: MenuItemType, indent: integer)
		if item.type == "separator" then
			table.insert(items, r.div { className = "menu-separator" })
		else
			local id = nextId(counter)
			local content: Element = nil
			local expandFn: function() = nil

			if item.type == "multiselect" then
				content = MenuSelector { selected = "itemA" }
			elseif item.type == "checkbox" then
				content = MenuCheckbox { checked = true }
			elseif item.type == "action" then
			elseif item.type == "submenu" then
				expandFn = function()
					setExpandState(function(cur: ExpandState): ExpandState
						local s = {}
						for k, v in pairs(cur) do s[k] = v end
						s[item.key] = not (s[item.key] or false)
						return s
					end)
				end
			end

			table.insert(items, MenuRow{
				idx = id,
				indent = indent,
				selectedIdx = selectedId,
				setSelected = setSelected,
				onClick = expandFn,
				id .. " - " .. item.key .. " - " .. item.name,
				content
			})

			if item.type == "submenu" and item.children ~= nil then
				if expandState[item.key] then
					for _, v in ipairs(item.children) do
						renderMenu(v, indent + 1)
					end
				end
			end
		end
	end

	for _, v in ipairs(props.menu.children) do
		renderMenu(v, 0)
	end

	return r.div {
		className = "menu",
		tabIndex = 0,
		onKey = function(ev: fw.KeyEvent): boolean
			if ev.key == fw.VirtualKey.UpArrow and ev.down == true and selectedId > 0 then
				setSelectedId(selectedId - 1)
			end

			if ev.key == fw.VirtualKey.DownArrow and ev.down == true then
				setSelectedId(selectedId + 1)
			end
		end,
		children = items
	}
end)

local Basic = React.component(function (_: PropsMap): Element
	return r.div {
		className = "root",

		r.div {
			style = {
				backgroundColor = fw.Color4F(0.2, 0.5, 0.2, 1),
				width = 160 * 3,
				height = 144 * 3,

				border = 1,
				borderColor = fw.Color4F(1, 0, 0, 1),
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

				r.span { className = "title", "RetroPlug 0.4.0" },
				r.div { className = "title-separator" },
				Menu { menu = menuItems }
			}
		},
	}
end)

React.render(Basic {})
