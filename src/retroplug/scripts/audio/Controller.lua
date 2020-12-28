local class = require("class")
local createNativeMenu = require("MenuHelper")
local LuaMenu = require("Menu")
local GameboySystem = require("System")
local ComponentManager = require("ComponentManager")
local PpqGenerator = require("PpqGenerator")
local midi = require("midi")
local componentutil = require("util.component")
local ConfigLoader = require("ConfigLoader")
local InputConfig = require("InputConfigParser")
local const = require("const")
local serpent = require("serpent")

local Controller = class()
function Controller:init()
	self._menuLookup = nil
	self._components = {}
	self._systems = {}
	self._selectedIdx = 0
	self._transportRunning = false
	self._timeInfo = nil
	self._inputConfig = InputConfig()
	self._sampleRate = 44100

	self._ppqGen = PpqGenerator(24, function(ppq, offset)
		self:emit("onPpq", ppq, offset)
	end)
end

function Controller:setup(model, timeInfo, sampleRate)
	self._components = ComponentManager.createComponents()
	self._model = model
	self._timeInfo = timeInfo
	self._sampleRate = sampleRate
	self._ppqGen:setSampleRate(sampleRate)

	Project._componentState = componentutil.createState(self._components)
end

function Controller:setSampleRate(sampleRate)
	self._sampleRate = sampleRate
	self._ppqGen:setSampleRate(sampleRate)
end

function Controller:loadConfigFromPath(path)
	self._config = nil

	local ok, config = ConfigLoader.loadConfigFromPath(path)
	if ok then
		self._config = config
		return true
	end

	return false
end

function Controller:initProject()
	for i = 1, const.MAX_SYSTEMS, 1 do
		local instModel = self._model:getSystem(i - 1)
		if instModel ~= nil then
			local state = componentutil.createState(self._components)
			local system = GameboySystem(instModel, self._model:getButtonPresses(i - 1), state)
			table.insert(Project.systems, system)
		end
	end

	if #Project.systems > 0 then
		self:setActive(0)
	end

	self:emit("onSetup")
	self:emit("onReload")
end

function Controller:setActive(idx)
	self._selectedIdx = idx + 1

	local system = Project.systems[self._selectedIdx]
	Project.system = system
	System = system
end

function Controller:emit(name, ...)
	componentutil.emitComponentEvent(self._components, name, ...)
end

function Controller:update(frameCount)
	local ti = self._timeInfo
	if ti == nil then
		print("Time info not set")
		return
	end

	local trans = Project.transport
	trans.changed = false
	trans.started = false
	trans.stopped = false
	trans.paused = false

	if self._timeInfo.transportIsRunning ~= self._transportRunning then
		trans.changed = true

		if self._timeInfo.transportIsRunning == true then
			trans.state = TransportState.Playing
			trans.started = true
		else
			trans.state = TransportState.Stopped
			trans.stopped = true
			self._ppqGen:reset()
		end

		self._transportRunning = self._timeInfo.transportIsRunning
		self:emit("onTransportChanged", self._transportRunning)
	end

	if self._transportRunning == true then
		self._ppqGen:setTempo(ti.tempo)
		self._ppqGen:setCycleRange(ti.cycleStart, ti.cycleEnd)
		self._ppqGen:update(ti.ppqPos, frameCount)
	end

	self:emit("onUpdate", frameCount)

	--[[if self._transportRunning == true then
		local ppqTrigger, offset = ppq.generatePpq24(frameCount, self._sampleRate, self._timeInfo)
		if ppqTrigger == true then
			self:emit("onPpq", offset)
		end
	end]]
end

function Controller:onMidi(offset, statusByte, data1, data2)
	local channel = statusByte & 0x0F
	local status = statusByte >> 4

	local r = self._model:getSettings().midiRouting
	if status == midi.Status.System or r == MidiChannelRouting.SendToAll then
		local msg = midi.Message(offset, statusByte, data1, data2)
		for _, v in ipairs(Project.systems) do
			self:emit("onMidi", v, msg)
		end
	elseif r == MidiChannelRouting.OneChannelPerInstance then
		local target = Project.systems[channel]
		if target ~= nil then
			local msg = midi.Message(offset, statusByte, data1, data2)
			self:emit("onMidi", target, msg)
		end
	elseif r == MidiChannelRouting.FourChannelsPerInstance then
		local targetIdx = math.floor(channel / 4)
		local target = Project.systems[targetIdx + 1]
		if target ~= nil then
			local msg = midi.Message(offset, (channel | (status << 4)), data1, data2)
			self:emit("onMidi", target, msg)
		end
	end
end

function Controller:onMenu(idx, menus)
	local menu = LuaMenu()
	self:emit("onMenu", menu)

	local menuLookup = {}
	menus:add(createNativeMenu(menu, nil, LUA_MENU_ID_OFFSET, menuLookup))
	self._menuLookup = menuLookup
end

function Controller:onMenuResult(idx)
	if self._menuLookup ~= nil then
		local callback = self._menuLookup[idx]
		if callback ~= nil then callback() end
		self._menuLookup = nil
	end
end

function Controller:onDialogResult(paths)
	--dialog.__onResult(paths)
end

function Controller:addSystem(idx, model, componentState)
	local state = componentutil.createState(self._components)
	local system = GameboySystem(model, self._model:getButtonPresses(idx), state)

	if componentState ~= nil and componentState ~= "" then
		local ok, state = serpent.load(componentState)
		if ok == true then system.state = state end
	end

	Project.systems[idx + 1] = system

	if self._selectedIdx == idx + 1 then
		self:setActive(idx)
	end
end

function Controller:duplicateSystem(sourceIdx, targetIdx, model)
	local state = componentutil.createState(self._components)
	local system = GameboySystem(model, self._model:getButtonPresses(targetIdx), state)
	Project.systems[targetIdx + 1] = system

	if self._selectedIdx == targetIdx + 1 then
		self:setActive(targetIdx)
	end

	local instData = serpent.block(Project.systems[sourceIdx + 1].state, { comment = false })
	local ok, state = serpent.load(instData)
	if ok == true then
		log.obj(state)
		Project.systems[targetIdx + 1].state = state
	end
end

function Controller:removeSystem(idx)
	table.remove(Project.systems, idx + 1)

	if self._selectedIdx >= #Project.systems then
		self._selectedIdx = #Project.systems
	end
end

function Controller:serializeSystem(idx, pretty)
	local opts = { comment = false }
	if pretty == true then opts.indent = '\t' end
	return serpent.block(Project.systems[idx + 1].state, opts)
end

function Controller:serializeSystems(pretty)
	local opts = { comment = false }
	if pretty == true then opts.indent = '\t' end

	local instances = {}
	for i, v in ipairs(Project.systems) do
		table.insert(instances, v.state)
	end

	return serpent.block(instances, opts)
end

function Controller:deserializeSystems(data)
	local ok, state = serpent.load(data)
	if ok == true then
		for i, v in ipairs(state) do
			local system = Project.systems[i]
			if system ~= nil then
				system.state = v
			end
		end
	end
end

function Controller:deserializeSystem(idx, data)
	local system = Project.systems[idx]
	if system == nil then
		log.warn("Failed to deserialize instance - instance " .. idx .. " does not exist")
		return
	end

	local ok, state = serpent.load(data)
	if ok == true then
		system.state = state
	end
end

function Controller:closeProject()
	Project.clear()
end

function Controller:loadInputConfig(path)
	self._inputConfig:load(path)
end

return Controller
