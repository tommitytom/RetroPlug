local class = require("class")
local util = require("util")
local const = require("const")
local log = require("log")
local fs = require("fs")
local pathutil = require("pathutil")
local fileutil = require("util.file")
local Error = require("Error")
local Globals = require("Globals")

local _ctx

local System = class()

function System.fromSystemDesc(desc)
	local system = System()
	system.desc = desc
	system._desc = desc
	return system
end

function System:init(desc, model, state)
	_ctx = Globals.audioContext

	assert(desc == nil)
	assert(model == nil)
	assert(state == nil)
	self._desc = nil

	self.state = {}
	self.inputMap = nil
end

function System:setInputMap(inputMap)
	self.inputMap = inputMap
	self._desc.keyInputConfig = inputMap.key.filename
	self._desc.padInputConfig = inputMap.pad.filename
end

function System:clone()
	return System.fromSystemDesc(SystemDesc.new(self._desc))
end

function System:setSram(data, reset)
	--self:emit("onSramSet", data)

	_ctx:setSram(self._desc.idx, data, reset)
end

function System:clearSram(reset)
	local buf = DataBuffer.new(const.SRAM_SIZE)
	buf:clear()
	--self:emit("onSramClear", buf)

	_ctx:setSram(self._desc.idx, buf, reset)
end

function System:setRom(data, reset)
	if reset == nil then reset = false end
	self._desc.romData = data
	self._desc.romName = util.getRomName(data)
	--self:emit("onRomSet", data)

	_ctx:setRom(self._desc.idx, data, reset)
end

-- Loads a ROM from a path string, or data buffer.  Rebuilds the
-- component array and resets the emulator.  The 'path' parameter
-- allows you to pass a path for metadata purposes if the value passed
-- to 'data' is a buffer.
function System:loadRom(data, path)
	local d = self._desc

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
				else
					break
				end
			end
		end
	else
		fileData, err = fileutil.loadPathOrData(data)
	end

	if err ~= nil then return err end

	if type(data) == "string" then path = data end

	d.systemType = SystemType.SameBoy
	d.state = SystemState.Initialized
	d.romData = fileData
	d.romName = util.getRomName(fileData)

	if path ~= nil then
		d.romPath = path

		local savPath = pathutil.changeExt(path, "sav")
		if fs.exists(savPath) == true then
			local savData = fs.load(savPath, false)

			if savData ~= nil then
				d.sramPath = savPath
				d.sramData = savData
			end
		end
	end

	_ctx:loadRom(d)
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
		self._desc.sramPath = path
	end

	--self:emit("onSramLoad", data)

	_ctx:setSram(self._desc.idx, data, reset)
end

function System:loadState(data, reset)
	if type(data) == "string" then
		data = fs.load(data)
		if data == nil then
			return Error("Failed to load state: " .. data .. " not valid")
		end
	end

	--self:emit("onStateLoad", data)

	_ctx:setState(self._desc.idx, data, reset)
end

function System:saveSram(path)
	if path == nil then
		path = self._desc.sramPath
	end

	if path == nil then
		path = pathutil.changeExt(self._desc.romPath, "sav")
	end

	log.info("Saving SRAM to " .. path)

	self._desc.sramPath = path

	return fs.save(path, self._desc.sramData)
end

function System:saveRom(path)
	return fs.save(path, self._desc.romData)
end

function System:saveState(path)
	if path == nil then
		path = pathutil.changeExt(self._desc.romPath, "state")
	end

	log.info("Saving state to " .. path)

	local req = FetchStateRequest.new()
	req.systems[self._desc.idx + 1] = ResourceType.State

	_ctx:fetchResources(req, function(systemStates)
		local data = systemStates.states[self._desc.idx + 1]
		if not isNullPtr(data) then
			return fs.save(path, data)
		else
			log.error("Failed to fetch state at index " .. self._desc.idx);
		end
	end)
end

function System:sram()
	return self._desc.sramData
end

function System:rom()
	return self._desc.romData
end

function System:buttons()
	return self._desc.buttons
end

function System:reset(model)
	if model == nil or model == GameboyModel.Auto then
		model = self._desc.sameBoySettings.model
	else
		self._desc.sameBoySettings.model = model
	end

	_ctx:resetSystem(self._desc.idx, model)
end

function System:updateSettings()
	_ctx:updateSystemSettings(self._desc.idx)
end

return System
