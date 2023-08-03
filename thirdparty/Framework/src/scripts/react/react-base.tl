require("base")

local type ComponentFunc = function(ElementFunc): ElementFunc
local type ComponentFuncNoProps = function(function(): Element): ElementFunc

local type React = record<ElementT>
	createElement: function(string|ElementFunc, PropsMap): Element
	render: function(Element, ElementT)
	useState: function<T>(T): T, function(ActionFunc<T>)
	workLoop: function(function(): number)
	component: ComponentFunc
	getCurrentRoot: function(): Fiber
end

return function<DomElementT, PropsT, EventsT>(reconciler: Reconciler<DomElementT, PropsT, EventsT>): React<DomElementT>
	local type StateHook = record<T>
		state: T
		queue: {ActionFunc<T>}
	end

	local record AnonStateHook
		state: any
		queue: {ActionFunc<any>}
	end

	local record Fiber
		type: string|ElementFunc
		props: PropsMap
		parent: Fiber
		child: Fiber
		sibling: Fiber
		alternate: Fiber
		dom: DomElementT
		effectTag: string
		hooks: {AnonStateHook}
	end

	local function createTextElement(text: string): Element
		return {
			type = "TEXT_ELEMENT",
			props = {
				nodeValue = text,
				children = {},
			},
		}
	end

	local function createElement(constructor: string|ElementFunc, props: PropsMap): Element
		if props.children == nil then
			props.children = {}
		end

		local children = props.children as {string|Element}
		for i,v in ipairs(children) do
			if v is string then
				children[i] = createTextElement(v)
			end
		end

		return {
			type = constructor,
			props = props,
		}
	end

	local function isEvent(key: string): boolean
		return string.sub(key, 1, 2) == "on"
	end

	local function isProperty(key: string): boolean
		return key ~= "children" and not isEvent(key)
	end

	local function isNew(prevProps: PropsMap, nextProps: PropsMap): function(key: string): boolean
		return function(key: string): boolean
			return prevProps[key] ~= nextProps[key]
		end
	end

	local function isGone(nextProps: PropsMap): function(key: string): boolean
		return function(key: string): boolean
			return not (nextProps[key] ~= nil)
		end
	end

	local function updateDom(dom: DomElementT, prevProps: PropsMap, nextProps: PropsMap)
		--local style = reconciler.getStyle(dom) as { string: any }

		-- Remove old or changed event listeners
		for name, prevValue in pairs(prevProps) do
			if isEvent(name) and (not nextProps[name] or isNew(prevProps, nextProps)(name)) then
				assert(type(prevValue) == "function")
				local func = prevValue as function(any): any
				reconciler.removeEventListener(dom, name, func)
			end
		end

		-- Remove old properties
		for name, _ in pairs(prevProps) do
			if isProperty(name) and isGone(nextProps)(name) then
				print("", "nulling", name)
				--style[name] = nil
				--reconciler.setProperty(dom, name, nil)
			end
		end

		-- Set new or changed properties
		for name, nextValue in pairs(nextProps) do
			if isProperty(name) and isNew(prevProps, nextProps)(name) then
				print("", "setting", name, nextValue)
				--TODO: Sanitize!
				--style[name] = nextValue
				reconciler.setProperty(dom, name, nextValue)
			end
		end

		-- Add event listeners
		for name, nextValue in pairs(nextProps) do
			if isEvent(name) and isNew(prevProps, nextProps)(name) then
				assert(type(nextValue) == "function")
				local func = nextValue as function(any): any
				reconciler.addEventListener(dom, name, func)
			end
		end
	end

	local function createDom(fiber: Fiber): DomElementT
		local dom: DomElementT
		local t = fiber.type as string
		assert(t is string)

		if t == "TEXT_ELEMENT" then
			dom = reconciler.createTextNode("")
		else
			dom = reconciler.createElement(t)
		end

		updateDom(dom, {}, fiber.props)

		return dom
	end

	local function commitDeletion(fiber: Fiber, domParent: DomElementT)
		if fiber.dom then
			reconciler.removeChild(domParent, fiber.dom)
		else
			assert(fiber.child ~= nil)
			commitDeletion(fiber.child, domParent)
		end
	end

	local function commitWork(fiber: Fiber)
		if fiber == nil then
			return
		end

		assert(fiber.parent)

		local domParentFiber = fiber.parent
		while not domParentFiber.dom do
			assert(domParentFiber.parent)
			domParentFiber = domParentFiber.parent
		end

		local domParent = domParentFiber.dom
		assert(domParent ~= nil)

		print(fiber.effectTag, fiber.type)

		if fiber.effectTag == "PLACEMENT" and fiber.dom then
			reconciler.appendChild(domParent, fiber.dom)
		elseif fiber.effectTag == "UPDATE" and fiber.dom then
			updateDom(fiber.dom, fiber.alternate.props, fiber.props)
		elseif fiber.effectTag == "DELETION" then
			commitDeletion(fiber, domParent)
		end

		commitWork(fiber.child)
		commitWork(fiber.sibling)
	end

	local _nextUnitOfWork: Fiber = nil
	local _currentRoot: Fiber = nil
	local _wipRoot: Fiber = nil
	local _deletions: {Fiber} = {}

	local function render(element: Element, container: DomElementT)
		if container == nil then
			container = reconciler.getRootElement()
		end

		_wipRoot = {
			dom = container,
			props = {
				children = { element },
			},
			alternate = _currentRoot
		}
		_deletions = {}
		_nextUnitOfWork = _wipRoot
	end

	local function reconcileChildren(wipFiber: Fiber, elements: {Element})
		local index = 1
		local oldFiber: Fiber = wipFiber.alternate and wipFiber.alternate.child
		local prevSibling: Fiber = nil

		while index <= #elements or oldFiber ~= nil do
			local element = elements[index]
			local sameType: boolean = oldFiber ~= nil and element ~= nil and element.type == oldFiber.type
			local newFiber: Fiber = nil

			if sameType then
				newFiber = {
					type = oldFiber.type,
					props = element.props,
					dom = oldFiber.dom,
					parent = wipFiber,
					alternate = oldFiber,
					effectTag = "UPDATE",
				}
			end

			if element and not sameType then
				newFiber = {
					type = element.type,
					props = element.props,
					dom = nil,
					parent = wipFiber,
					alternate = nil,
					effectTag = "PLACEMENT",
				}
			end

			if oldFiber and not sameType then
				oldFiber.effectTag = "DELETION"
				table.insert(_deletions, oldFiber)
			end

			if oldFiber then
				oldFiber = oldFiber.sibling
			end

			if index == 1 then
				wipFiber.child = newFiber
			elseif element then
				assert(prevSibling)
				prevSibling.sibling = newFiber
			end

			prevSibling = newFiber
			index = index + 1
		end
	end

	local function updateHostComponent(fiber: Fiber)
		if not fiber.dom then
			fiber.dom = createDom(fiber)
		end

		reconcileChildren(fiber, fiber.props.children as {Element})
	end

	local _wipFiber: Fiber = nil
	local _hookIndex: integer = nil

	local function updateFunctionComponent(fiber: Fiber)
		_wipFiber = fiber
		assert(_wipFiber)
		_hookIndex = 1
		fiber.hooks = {}

		local func = fiber.type as ElementFunc
		reconcileChildren(fiber, { func(fiber.props) })
	end

	local function performUnitOfWork(fiber: Fiber): Fiber
		print("", fiber.type)
		if type(fiber.type) == "function" then
			updateFunctionComponent(fiber)
		else
			updateHostComponent(fiber)
		end

		if fiber.child then
			return fiber.child
		end

		local nextFiber: Fiber = fiber
		while nextFiber do
			if nextFiber.sibling then
				return nextFiber.sibling
			end

			nextFiber = nextFiber.parent
		end

		return nil
	end

	local function useState<T>(initial: T): T, function(ActionFunc<T>)
		assert(_wipFiber ~= nil)
		assert(_wipFiber.hooks ~= nil)
		assert(_hookIndex ~= nil)

		local oldHook: StateHook<T> =
			_wipFiber.alternate and
			_wipFiber.alternate.hooks and
			_wipFiber.alternate.hooks[_hookIndex] as StateHook<T>

		local hook: StateHook<T> = {
			state = oldHook ~= nil and oldHook.state or initial,
			queue = {},
		}

		local actions: {ActionFunc<T>} = oldHook ~= nil and oldHook.queue or {}
		for _, action in ipairs(actions) do
			hook.state = action(hook.state)
		end

		local function setState(action: ActionFunc<T>)
			assert(_currentRoot)

			table.insert(hook.queue, action)

			_wipRoot = {
				dom = _currentRoot.dom,
				props = _currentRoot.props,
				alternate = _currentRoot,
			}
			_nextUnitOfWork = _wipRoot
			_deletions = {}
		end

		table.insert(_wipFiber.hooks, hook as AnonStateHook)
		_hookIndex = _hookIndex + 1
		return hook.state, setState
	end

	local function commitRoot()
		print("", "committing root")
		for _, fiber in ipairs(_deletions) do
			commitWork(fiber)
		end

		assert(_wipRoot)

		commitWork(_wipRoot.child)
		_currentRoot = _wipRoot
		_wipRoot = nil
	end

	local function workLoop(deadline: function(): number)
		local shouldYield: boolean = false
		while _nextUnitOfWork and not shouldYield do
			_nextUnitOfWork = performUnitOfWork(_nextUnitOfWork)
			shouldYield = deadline() < 1
		end

		if not _nextUnitOfWork and _wipRoot then
			commitRoot()
		end
	end

	local function component(func: ElementFunc): function(PropsMap): Element
		return function(props: PropsMap): Element
			return createElement(func, props)
		end
	end

	return {
		createElement = createElement,
		render = render,
		useState = useState,
		workLoop = workLoop,
		component = component,
		getCurrentRoot = function(): Fiber return _currentRoot end,
	}
end
