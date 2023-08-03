local document = fw.registry

local globalTree = nil

local function findNode(node, entity)
	if node.entity == entity then
		return node
	end

	if node.element then
		local n = findNode(node.element, entity)
		if n then return n end
	end

	for _, v in ipairs(node.children) do
		local n = findNode(v, entity)
		if n then return n end
	end
end

function processHostEvent(entity, name, ev)
	local node = findNode(globalTree, entity)
	if node then
		local eventProp = node.props[name]
		if eventProp then
			eventProp(ev)
			return true
		end
	end

	return false
end

local function Div(entity, props)
	local style = document:getNodeStyle(entity)

	if props.border ~= nil then
		if type(props.border) == "number" then
			style.border = fw.FlexBorder(props.border, props.border, props.border, props.border)
		else
			style.border = props.border
		end
	end

	if props.padding ~= nil then
		if type(props.padding) == "number" then
			local v = fw.FlexValue(props.padding)
			style.padding = fw.FlexRect(v, v, v, v)
		else
			style.padding = props.padding
		end
	end

	if props.margin ~= nil then
		if type(props.margin) == "number" then
			local v = fw.FlexValue(props.margin)
			style.margin = fw.FlexRect(v, v, v, v)
		else
			style.margin = props.margin
		end
	end

	if props.position ~= nil then style.position = props.position end

	if props.flexDirection ~= nil then style.flexDirection = props.flexDirection end
	if props.justifyContent ~= nil then style.justifyContent = props.justifyContent end
	if props.flexAlignItems ~= nil then style.flexAlignItems = props.flexAlignItems end
	if props.flexAlignSelf ~= nil then style.flexAlignSelf = props.flexAlignSelf end
	if props.flexAlignContent ~= nil then style.flexAlignContent = props.flexAlignContent end
	if props.layoutDirection ~= nil then style.layoutDirection = props.layoutDirection end
	if props.flexWrap ~= nil then style.flexWrap = props.flexWrap end
	if props.flexGrow ~= nil then style.flexGrow = props.flexGrow end
	if props.flexShrink ~= nil then style.flexShrink = props.flexShrink end
	if props.flexBasis ~= nil then style.flexBasis = props.flexBasis end
	if props.minWidth ~= nil then style.minWidth = props.minWidth end
	if props.maxWidth ~= nil then style.maxWidth = props.maxWidth end
	if props.minHeight ~= nil then style.minHeight = props.minHeight end
	if props.maxHeight ~= nil then style.maxHeight = props.maxHeight end
	if props.width ~= nil then style.width = props.width end
	if props.height ~= nil then style.height = props.height end
	if props.aspectRatio ~= nil then style.aspectRatio = props.aspectRatio end
	if props.overflow ~= nil then style.overflow = props.overflow end

	if props.color ~= nil then document:setNodeColor(entity, props.color) end
	if props.backgroundColor ~= nil then document:setNodeBackgroundColor(entity, props.backgroundColor) end
end

local function Text(entity, props)
	Div(entity, props)
	document:setNodeText(entity, props.text)
end

local systemComponents = {
	[Div] = true,
	[Text] = true
}

local function isSystemComponent(component)
	return systemComponents[component] == true
end

local function h(component, props, children)
	if type(component) == "string" then
		if component == "div" then component = Div
		elseif component == "p" then component = Text
		else
			error("Unknown component: " .. component)
		end
	end

	if type(component) == "function" then
		if isSystemComponent(component) then
			component = {
				type = "system",
				func = component
			}
		else
			component = {
				type = "function",
				func = component
			}
		end
	end

	return {
	  component = component,
	  props = props or {},
	  children = children or {},
	  element = nil
	}
end

local function useState(value)
	return value, function(v) print("set value:", value) end
end

local function Button(props)
	local mouseOver, setMouseOver = useState(false)

	local backgroundColor = mouseOver and fw.Color4F(0.6, 0.6, 0.6, 1) or fw.Color4F(0.4, 0.4, 0.4, 1)

	return h(Div, {
		--flexDirection = fw.FlexDirection.Column,
		--justifyContent = fw.FlexJustify.FlexStart,
		--flexAlignItems = fw.FlexAlign.FlexStart,
		--flexAlignContent = fw.FlexAlign.Stretch,
		--backgroundColor = fw.Color4F(0.4, 0.4, 0.4, 1),
		border = 2,
		onMouseEnter = function() setMouseOver(true) end,
		onMouseLeave = function() setMouseOver(false) end,
		onMouseMove = function(ev) print(ev) end
	}, {
		h(Text, {
			text = props.text,
			backgroundColor = backgroundColor,
			padding = 20
		})
	})
end

local function MyButton(props)
	return h(Button, { text = "Shiiiit" })
end

local function App()
	return h(Div, { id = "app", backgroundColor = fw.Color4F(0, 1, 0, 1), flexDirection = fw.FlexDirection.Row }, {
		h(Text, { text = "Hello Worlds!", backgroundColor = fw.Color4F(1, 0, 0, 1), border = 10 }),
		h(Text, { text = "Worlds, Hello!", backgroundColor = fw.Color4F(0, 0, 1, 1), border = 10 }),
		h(Button, { text = "Bu!", backgroundColor = fw.Color4F(1, 0, 0, 1) }),
		h(Text, { text = "This is a paragraph", backgroundColor = fw.Color4F(1, 0, 0, 1) })
	})
end

local function buildTree(node, indent)
	print(indent .. (node.props.id or "<>"))

	if node.component.type == "function" then
		node.element = node.component.func(node.props)
		buildTree(node.element, indent .. "  ")
	end

	for _, v in ipairs(node.children) do
		buildTree(v, indent .. "  ")
	end

	return node
end

local function renderNode(entity, node, indent)
	if node.component.type == "system" then
		node.entity = entity
		node.component.func(entity, node.props)
	elseif node.element then
		renderNode(entity, node.element, indent)
	end

	for _, v in ipairs(node.children) do
		local child = document:pushBack(entity)
		print(indent .. "> " .. (v.props.id or "<>"))
		renderNode(child, v, indent .. "  ")
	end
end

local function run()
	local tree = buildTree(App(), "")

	local e = document:getRootEntity()
	print("> root")
	renderNode(e, tree, "  ")

	globalTree = tree

	--print(fw.PointF32)
	--a = fw.PointF32()
	--a.x = 100
	--print(a.x)
end

local function errorHandler(err)
	print("ERROR: " .. err)
end

xpcall(run, errorHandler)
