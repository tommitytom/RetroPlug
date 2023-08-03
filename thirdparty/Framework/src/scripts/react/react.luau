--!strict

type DomStyle = {
	flexGrow: number?
};

type DomElementHandle = number;

type Reconciler = {
	appendChild: (DomElementHandle, DomElementHandle) -> (),
	removeChild: (DomElementHandle, DomElementHandle) -> (),
	addEventListener: (DomElementHandle, string, any) -> (),
	removeEventListener: (DomElementHandle, string, any) -> (),
	getStyle: (DomElementHandle) -> (DomStyle),
	createElement: (string) -> (DomElementHandle),
	createTextNode: (string) -> (DomElementHandle),
	getRootElement: () -> (DomElementHandle)
}

type PropsMap = {[string]: any};
type HooksMap = {[number]: any};
type ElementFunc = (PropsMap) -> Element;
type ActionFunc = (any) -> any;

type Fiber = {
	type: string?|ElementFunc?,
	props: PropsMap,
	parent: Fiber?,
	child: Fiber?,
	sibling: Fiber?,
	alternate: Fiber?,
	dom: DomElementHandle?,
	effectTag: string?,
	hooks: HooksMap?
}

type Element = {
	type: string?|ElementFunc?,
	props: PropsMap,
}

local function createTextElement(text: string): Element
	return {
		type = "TEXT_ELEMENT",
		props = {
			nodeValue = text,
			children = {},
		},
	}
end

local function createElement(constructor: string?|ElementFunc?, props: PropsMap): Element
	if props.children == nil then
		props.children = {}
	end

	for i,v in ipairs(props.children) do
		if type(v) == "string" then
			props.children[i] = createTextElement(v)
		end
	end

	return {
		type = constructor,
		props = props,
	}
end

local function isEvent(key: string)
	return string.sub(key, 1, 2) == "on"
end

local function isProperty(key: string)
	return key ~= "children" and not isEvent(key)
end

local function isNew(prev, next)
	return function(key)
		return prev[key] ~= next[key]
	end
end

local function isGone(prev, next)
	return function(key)
		return not (next[key] ~= nil)
	end
end

return function(reconciler: Reconciler)
	local function updateDom(dom: DomElementHandle, prevProps: PropsMap, nextProps: PropsMap)
		local style = reconciler.getStyle(dom)

		-- Remove old or changed event listeners
		for name, prevValue in pairs(prevProps) do
			if isEvent(name) and (not nextProps[name] or isNew(prevProps, nextProps)(name)) then
				assert(type(prevValue) == "function")
				reconciler.removeEventListener(dom, name, prevValue)
			end
		end

		-- Remove old properties
		for name, prevValue in pairs(prevProps) do
			if isProperty(name) and isGone(prevProps, nextProps)(name) then
				print("", "nulling", name)
				style[name] = nil
			end
		end

		-- Set new or changed properties
		for name, nextValue in pairs(nextProps) do
			if isProperty(name) and isNew(prevProps, nextProps)(name) then
				print("", "setting", name, nextValue)
				--TODO: Sanitize!
				style[name] = nextValue
			end
		end

		-- Add event listeners
		for name, nextValue in pairs(nextProps) do
			if isEvent(name) and isNew(prevProps, nextProps)(name) then
				assert(type(nextValue) == "function")
				reconciler.addEventListener(dom, name, nextValue)
			end
		end
	end

	local function createDom(fiber: Fiber)
		local dom: DomElementHandle
		print(fiber.type)
		if fiber.type == "TEXT_ELEMENT" then
			dom = reconciler.createTextNode("")
		else
			assert(type(fiber.type) == "string")
			dom = reconciler.createElement(fiber.type)
		end

		updateDom(dom, {}, fiber.props)

		return dom
	end

	local function commitDeletion(fiber: Fiber, domParent: DomElementHandle)
		if fiber.dom then
			reconciler.removeChild(domParent, fiber.dom)
		else
			assert(fiber.child ~= nil)
			commitDeletion(fiber.child, domParent)
		end
	end

	local function commitWork(fiber: Fiber?)
		if not fiber then
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
			assert(fiber.alternate)
			updateDom(fiber.dom, fiber.alternate.props, fiber.props)
		elseif fiber.effectTag == "DELETION" then
			commitDeletion(fiber, domParent)
		end

		commitWork(fiber.child)
		commitWork(fiber.sibling)
	end

	local _nextUnitOfWork: Fiber? = nil
	local _currentRoot: Fiber? = nil
	local _wipRoot: Fiber? = nil
	local _deletions: {[number]: Fiber} = {}

	local function render(element: Element, container: DomElementHandle?)
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

	local function reconcileChildren(wipFiber: Fiber, elements: {[number]: Element})
		local index = 1
		local oldFiber: Fiber? = wipFiber.alternate and wipFiber.alternate.child
		local prevSibling: Fiber? = nil

		while index <= #elements or oldFiber do
			local element = elements[index]
			local newFiber: Fiber? = nil

			local sameType: boolean = oldFiber ~= nil and element ~= nil and element.type == oldFiber.type

			if sameType then
				assert(oldFiber)

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
		reconcileChildren(fiber, fiber.props.children)
	end

	local _wipFiber: Fiber? = nil
	local _hookIndex: number? = nil

	local function updateFunctionComponent(fiber: Fiber)
		_wipFiber = fiber
		assert(_wipFiber)
		_hookIndex = 1
		fiber.hooks = {}

		assert(type(fiber.type) == "function")

		local children = { fiber.type(fiber.props) }
		reconcileChildren(fiber, children)
	end

	local function performUnitOfWork(fiber: Fiber): Fiber?
		print("", fiber.type)
		if type(fiber.type) == "function" then
			updateFunctionComponent(fiber)
		else
			updateHostComponent(fiber)
		end
		if fiber.child then
			return fiber.child
		end

		local nextFiber: Fiber? = fiber
		while nextFiber do
			if nextFiber.sibling then
				return nextFiber.sibling
			end
			nextFiber = nextFiber.parent
		end

		return nil
	end



	local function useState(initial)
		assert(_wipFiber ~= nil)
		assert(_hookIndex ~= nil)

		local oldHook =
			_wipFiber.alternate and
			_wipFiber.alternate.hooks and
			_wipFiber.alternate.hooks[_hookIndex]

		local hook = {
			state = oldHook and oldHook.state or initial,
			queue = {},
		}

		local actions = oldHook and oldHook.queue or {}
		for _, action in ipairs(actions) do
			hook.state = action(hook.state)
		end

		local function setState(action: ActionFunc)
			table.insert(hook.queue, action)
			assert(_currentRoot)

			_wipRoot = {
				dom = _currentRoot.dom,
				props = _currentRoot.props,
				alternate = _currentRoot,
			}
			_nextUnitOfWork = _wipRoot
			_deletions = {}
		end

		assert(_wipFiber.hooks)

		table.insert(_wipFiber.hooks, hook)
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

	local function workLoop(deadline: () -> (number))
		local shouldYield: boolean = false
		while _nextUnitOfWork and not shouldYield do
			_nextUnitOfWork = performUnitOfWork(_nextUnitOfWork)
			shouldYield = deadline() < 1
		end

		if not _nextUnitOfWork and _wipRoot then
			commitRoot()
		end
	end

	local function component(func: (PropsMap) -> (Element))
		return function(props: PropsMap)
			return createElement(func, props)
		end
	end

	return {
		createElement = createElement,
		render = render,
		useState = useState,
		workLoop = workLoop,
		component = component,
		getCurrentRoot = function() return _currentRoot end
	}
end
