require("component")
require("constants")
require("components.ButtonHandler")
require("components.GlobalButtonHandler")
local inspect = require("inspect")

Action = {}
setmetatable(Action, {
	__index = function(table, componentName)
		local actionNameTable = {}
		setmetatable(actionNameTable, {
			__index = function(table, actionName)
				return {
					component = componentName,
					action = actionName
				}
			end
		})

		return actionNameTable
	end
})

local MAX_INSTANCES = 4

Active = nil
local _activeIdx = 0
local _instances = {}
local _keyState = {}

local _actions = {}

local _globalComponents = {}

local _componentFactory = {
	instance = {},
	global = {}
}

local function componentInputRoute(target, ...)
	local handled = false
	for _, v in ipairs(_globalComponents) do
		local found = v[target]
		if found ~= nil then
			found(v, ...)
			handled = true
		end
	end

	if Active ~= nil then
		for _, v in ipairs(Active.components) do
			local found = v[target]
			if found ~= nil then
				found(v, ...)
				handled = true
			end
		end
	end

	return handled
end

local function findInstanceComponents(emulatorDesc, buttons)
	local components = {}
	for _, v in ipairs(_componentFactory.instance) do
		local d = v.__desc
		if d.romName == nil or emulatorDesc.romName:find(d.romName) ~= nil then
			print("Attaching component " .. d.name)
			table.insert(components, v.new(emulatorDesc, buttons))
		end
	end

	return components
end

local function runComponentHandler(components, handlerName, ...)
	for _, component in ipairs(components) do
		local found = component[handlerName]
		if found ~= nil then found(component, ...) end
	end
end

function _loadComponent(name)
	local component = require("components/" .. name)
	if component ~= nil then
		print("Registered component: " .. component.__desc.name)
		if component.__desc.global == true then
			table.insert(_componentFactory.global, component)
		else
			table.insert(_componentFactory.instance, component)
		end
	else
		print("Failed to load " .. name .. ": Script does not return a component")
	end
end

local function extractActions(components)
	local actions = {}
	for _, v in ipairs(components) do
		if #v.__actions > 0 then
			actions[v.__desc.name] = v.__actions
		end
	end

	return actions
end

function _init()
	for _, v in ipairs(_componentFactory.global) do
		table.insert(_globalComponents, v.new())
	end

	for i = 1, MAX_INSTANCES, 1 do
		local desc = _proxy:getInstance(i - 1)
		if desc.state ~= EmulatorInstanceState.Uninitialized then
			local buttons = _proxy:buttons(i - 1)
			local instance = {
				desc = desc,
				components = findInstanceComponents(desc, buttons),
				buttons = buttons
			}

			table.insert(_instances, instance)

			if i == _proxy:activeInstanceIdx() + 1 then
				_activeIdx = i
				Active = _instances[i]
			end

			_actions[i] = extractActions(instance.components)
		end
	end

	for _, instance in ipairs(_instances) do
		runComponentHandler(instance.components, "onComponentsInitialized", instance.components)
		runComponentHandler(instance.components, "onReload", instance.desc)
	end
end

local function addInstance(desc)
	if #_instances < MAX_INSTANCES then
		desc.idx = #_instances

		local instance = {
			desc = desc,
			components = {},
			buttons = _proxy:buttons(desc.idx)
		}

		table.insert(_instances, instance)

		if #_instances == 1 then
			Active = _instances[1]
		end

		return instance
	end
end

function _removeInstance(index)
	if index + 1 == _activeIdx then
		_activeIdx = 0
		Active = nil
	end

	table.remove(_instances, index + 1)
	_proxy:removeInstance(index)
end

function _duplicateInstance(idx)
	if #_instances < MAX_INSTANCES then
		local source = _instances[idx + 1]

		local instance = {
			model = _proxy:duplicateInstance(idx),
			components = {},  -- TODO: Duplicate components
			buttons = _proxy:buttons(idx)
		}

		return instance.model
	end
end

function _loadRom(idx, path)
	local file = _proxy:fileManager():loadFile(path, false)
	if file == nil then
		return
	end

	local d = EmulatorInstanceDesc.new()
	d.idx = idx
	d.type = EmulatorType.SameBoy
	d.romPath = path
	d.romName = file.data:slice(0x0134, 15):toString()
	d.sourceRomData = file.data

	local instance
	if idx == -1 then
		instance = addInstance(d)
	else
		instance = _instances[idx + 1]
		if instance == nil then
			print("Failed to load " .. path .. ": Instance " .. idx .. " does not exist")
			return
		end
	end

	instance.components = findInstanceComponents(d, instance.buttons)
	_actions[idx + 1] = extractActions(instance.components)

	runComponentHandler(instance.components, "onComponentsInitialized", instance.components)
	runComponentHandler(instance.components, "onBeforeRomLoad", instance.desc)
	_proxy:setInstance(d)
	runComponentHandler(instance.components, "onRomLoad", instance.desc)
end

function _findRom(idx, path)
	--[[std::string originalPath = _proxy->getActiveInstance()->romPath;
	_proxy->instances()
	for (size_t i = 0; i < _views.size(); i++) {
		auto plug = _views[i]->Plug();
		if (plug->romPath() == originalPath) {
			plug->init(paths[0], plug->model(), true);
			plug->disableRendering(false);
			_views[i]->HideText();
		}
	}]]
end

function _setActive(idx)
	_activeIdx = idx + 1
	Active = _instances[_activeIdx]
	_proxy:setActiveInstance(idx)
end

function _frame(delta)

end

function _onKey(key, down)
	local vk = key.vk
	if down == true then
		if _keyState[vk] ~= nil then return end
		_keyState[vk] = true
	else
		_keyState[vk] = nil
	end

	return componentInputRoute("onKey", vk, down)
end

function _onPadButton(button, down)
	return componentInputRoute("onPadButton", button, down)
end

function _onMidi(note, down)
	--pluginInputRoute("onMidi", note, down)
end

function _onDrop(str)
	return componentInputRoute("onDrop", str)
end

Action.RetroPlug = {
	NextInstance = function(down)
		if down == true then
			local nextIdx = _activeIdx
			if nextIdx == #_instances then
				nextIdx = 0
			end

			_setActive(nextIdx)
		end
	end
}
