local class = require("class")
local util = require("util")
local const = require("const")
local log = require("log")
local fs = require("fs")
local serpent = require("serpent")
local pathutil = require("pathutil")
local fileutil = require("util.file")
local componentutil = require("util.component")
local ComponentManager = require("ComponentManager")
local Error = require("Error")

local System = class()
function System:init(desc, model, config)
	self._audioContext = nil

	if type(desc) == "userdata" then
		if desc.__type.name == "SystemDesc" then
			self.desc = desc
			--local _, componentState = serpent.load(desc.uiComponentState)
		elseif desc.__type.name == "DataBuffer" then
			self:loadRom(desc)
		end
	elseif type(desc) == "string" then
		self:loadRom(desc)
	end

	if model ~= nil then self.desc.sameBoySettings.model = model end
	self._desc = self.desc
end

function System:clone()
	return System(SystemDesc.new(self.desc))
end

function System:setComponentData(componentName, data)
	self.componentData[componentName] = data
end

function System:setSram(data, reset)
	self:emit("onSramSet", data)

	if isNullPtr(self._audioContext) == false then
		self._audioContext:setSram(self.desc.idx, data, reset)
	end
end

function System:clearSram(reset)
	local buf = DataBuffer.new(const.SRAM_SIZE)
	buf:clear()
	self:emit("onSramClear", buf)

	if isNullPtr(self._audioContext) == false then
		self._audioContext:setSram(self.desc.idx, buf, reset)
	end
end

function System:setRom(data, reset)
	if reset == nil then reset = false end
	self.desc.romData = data
	self.desc.romName = util.getRomName(data)
	self:emit("onRomSet", data)

	if isNullPtr(self._audioContext) == false then
		self._audioContext:setRom(self.desc.idx, data, reset)
	end
end

local function createSystemDesc(config)
	local desc = SystemDesc.new()
	if config then
		desc.sameBoySettings.model = config.SameBoy.model
		desc.sameBoySettings.gameLink = config.SameBoy.gameLink
	end

	return desc
end

-- Loads a ROM from a path string, or data buffer.  Rebuilds the
-- component array and resets the emulator.  The 'path' parameter
-- allows you to pass a path for metadata purposes if the value passed
-- to 'data' is a buffer.
function System:loadRom(data, config, path)
	local fileData, err
	if type(data) == "string" and pathutil.ext(data) == "zip" then
		local zipReader = ZipReader.new(data)
		local entries = zipReader:entries()
		for _, v in ipairs(entries) do
			if pathutil.ext(v.name) == "gb" then
				fileData = zipReader:read(v.name)
				if isNullPtr(fileData) then
					fileData = nil
					err = Error("Failed to read ROM from zip file (" .. v.name .. ")")
				end
			end
		end
	else
		fileData, err = fileutil.loadPathOrData(data)
	end

	if err ~= nil then return err end

	if type(data) == "string" then path = data end
	if self.desc == nil then self.desc = createSystemDesc(config) end

	local d = self.desc
	local idx = d.idx
	d:clear()
	d.idx = idx

	if path ~= nil then
		d.romPath = path
	end

	d.emulatorType = SystemType.SameBoy
	d.state = SystemState.Initialized
	d.romData = fileData
	d.romName = util.getRomName(fileData)

	if d.romPath ~= nil then
		local savPath = pathutil.changeExt(d.romPath, "sav")
		if fs.exists(savPath) == true then
			local savData = fs.load(savPath, false)
			if savData ~= nil then
				d.sramPath = savPath
				d.sramData = savData
			end
		end
	end

	self.components = ComponentManager.createSystemComponents(self)

	self:emit("onComponentsInitialized", self.components)
	self:emit("onRomLoad")

	if isNullPtr(self._audioContext) == false then
		self._audioContext:loadRom(d)
	end
end


-- Loads a SAV from a path string, or data buffer.
-- The third parameter 'path' allows you to pass a path for metadata
-- purposes if the value passed to 'data' is a buffer.
function System:loadSram(data, reset, path)
	if type(data) == "string" then
		path = data
		data = fs.load(path)
	end

	if path ~= nil then
		self.desc.sramPath = path
	end

	self:emit("onSramLoad", data)

	if isNullPtr(self._audioContext) == false then
		self._audioContext:setSram(self.desc.idx, data, reset)
	end
end

function System:loadState(data, reset)
	if type(data) == "string" then
		data = fs.load(data)
		if data == nil then
			return Error("Failed to load state: " .. data .. " not valid")
		end
	end

	self:emit("onStateLoad", data)

	if isNullPtr(self._audioContext) == false then
		self._audioContext:setState(self.desc.idx, data, reset)
	end
end

function System:saveSram(path)
	return fs.save(path, self.desc.sramData)
end

function System:saveRom(path)
	return fs.save(path, self.desc.romData)
end

function System:saveState(path)
	local req = FetchStateRequest.new()
	req.systems[self.desc.idx + 1] = ResourceType.State

	self._audioContext:fetchResources(req, function(systemStates)
		local data = systemStates.states[self.desc.idx + 1]
		if not isNullPtr(data) then
			return fs.save(path, data)
		else
			log.error("Failed to fetch state at index " .. self.desc.idx);
		end
	end)
end

function System:sram()
	return self.desc.sramData
end

function System:rom()
	return self.desc.romData
end

function System:buttons()
	return self.desc.buttons
end

function System:reset(model)
	if model == nil or model == GameboyModel.Auto then
		model = self.desc.sameBoySettings.model
	else
		self.desc.sameBoySettings.model = model
	end

	if isNullPtr(self._audioContext) == false then
		self._audioContext:resetSystem(self.desc.idx, model)
	end
end

function System:updateSettings()
	self._audioContext:updateSystemSettings(self.desc.idx)
end

return System
