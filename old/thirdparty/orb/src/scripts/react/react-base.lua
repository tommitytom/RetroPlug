local _tl_compat; if (tonumber((_VERSION or ''):match('[%d.]*$')) or 0) < 5.3 then local p, m = pcall(require, 'compat53.module'); if p then _tl_compat = m end end; local assert = _tl_compat and _tl_compat.assert or assert; local ipairs = _tl_compat and _tl_compat.ipairs or ipairs; local pairs = _tl_compat and _tl_compat.pairs or pairs; local string = _tl_compat and _tl_compat.string or string; local table = _tl_compat and _tl_compat.table or table; require("base")


local ComponentFuncNoProps = {}










return function(reconciler)






















   local function createTextElement(text)
      return {
         type = "TEXT_ELEMENT",
         props = {
            nodeValue = text,
            children = {},
         },
      }
   end

   local function createElement(constructor, props)
      if props.children == nil then
         props.children = {}
      end

      local children = props.children
      for i, v in ipairs(children) do
         if type(v) == "string" then
            children[i] = createTextElement(v)
         end
      end

      return {
         type = constructor,
         props = props,
      }
   end

   local function isEvent(key)
      return string.sub(key, 1, 2) == "on"
   end

   local function isProperty(key)
      return key ~= "children" and not isEvent(key)
   end

   local function isNew(prevProps, nextProps)
      return function(key)
         return prevProps[key] ~= nextProps[key]
      end
   end

   local function isGone(nextProps)
      return function(key)
         return not (nextProps[key] ~= nil)
      end
   end

   local function updateDom(dom, prevProps, nextProps)



      for name, prevValue in pairs(prevProps) do
         if isEvent(name) and (not nextProps[name] or isNew(prevProps, nextProps)(name)) then
            assert(type(prevValue) == "function")
            local func = prevValue
            reconciler.removeEventListener(dom, name, func)
         end
      end


      for name, _ in pairs(prevProps) do
         if isProperty(name) and isGone(nextProps)(name) then
            print("", "nulling", name)

         end
      end


      for name, nextValue in pairs(nextProps) do
         if isProperty(name) and isNew(prevProps, nextProps)(name) then
            print("", "setting", name, nextValue)


         end
      end


      for name, nextValue in pairs(nextProps) do
         if isEvent(name) and isNew(prevProps, nextProps)(name) then
            assert(type(nextValue) == "function")
            local func = nextValue
            reconciler.addEventListener(dom, name, func)
         end
      end
   end

   local function createDom(fiber)
      local dom
      local t = fiber.type
      assert(type(t) == "string")

      if t == "TEXT_ELEMENT" then
         dom = reconciler.createTextNode("")
      else
         dom = reconciler.createElement(t)
      end

      updateDom(dom, {}, fiber.props)

      return dom
   end

   local function commitDeletion(fiber, domParent)
      if fiber.dom then
         reconciler.removeChild(domParent, fiber.dom)
      else
         assert(fiber.child ~= nil)
         commitDeletion(fiber.child, domParent)
      end
   end

   local function commitWork(fiber)
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
         updateDom(fiber.dom, fiber.alternate, fiber.props)
      elseif fiber.effectTag == "DELETION" then
         commitDeletion(fiber, domParent)
      end

      commitWork(fiber.child)
      commitWork(fiber.sibling)
   end

   local _nextUnitOfWork = nil
   local _currentRoot = nil
   local _wipRoot = nil
   local _deletions = {}

   local function render(element, container)
      if container == nil then
         container = reconciler.getRootElement()
      end

      _wipRoot = {
         dom = container,
         props = {
            children = { element },
         },
         alternate = _currentRoot,
      }
      _deletions = {}
      _nextUnitOfWork = _wipRoot
   end

   local function reconcileChildren(wipFiber, elements)
      local index = 1
      local oldFiber = wipFiber.alternate ~= nil and wipFiber.alternate.child
      local prevSibling = nil

      while index <= #elements or oldFiber ~= nil do
         local element = elements[index]
         local sameType = oldFiber ~= nil and element ~= nil and element.type == oldFiber.type
         local newFiber = nil

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

   local function updateHostComponent(fiber)
      if not fiber.dom then
         fiber.dom = createDom(fiber)
      end

      reconcileChildren(fiber, fiber.props.children)
   end

   local _wipFiber = nil
   local _hookIndex = nil

   local function updateFunctionComponent(fiber)
      _wipFiber = fiber
      assert(_wipFiber)
      _hookIndex = 1
      fiber.hooks = {}

      local func = fiber.type
      reconcileChildren(fiber, { func(fiber.props) })
   end

   local function performUnitOfWork(fiber)
      print("", fiber.type)
      if type(fiber.type) == "function" then
         updateFunctionComponent(fiber)
      else
         updateHostComponent(fiber)
      end

      if fiber.child then
         return fiber.child
      end

      local nextFiber = fiber
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
      assert(_wipFiber.hooks ~= nil)
      assert(_hookIndex ~= nil)

      local oldHook = 
      _wipFiber.alternate ~= nil and
      _wipFiber.alternate.hooks ~= nil and
      _wipFiber.alternate.hooks[_hookIndex]

      local hook = {
         state = oldHook ~= nil and oldHook.state or initial,
         queue = {},
      }

      local actions = oldHook ~= nil and oldHook.queue or {}
      for _, action in ipairs(actions) do
         hook.state = action(hook.state)
      end

      local function setState(action)
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

   local function workLoop(deadline)
      local shouldYield = false
      while _nextUnitOfWork and not shouldYield do
         _nextUnitOfWork = performUnitOfWork(_nextUnitOfWork)
         shouldYield = deadline() < 1
      end

      if not _nextUnitOfWork and _wipRoot then
         commitRoot()
      end
   end

   local function component(func)
      return function(props)
         return createElement(func, props)
      end
   end

   return {
      createElement = createElement,
      render = render,
      useState = useState,
      workLoop = workLoop,
      component = component,
      getCurrentRoot = function() return _currentRoot end,
   }
end
