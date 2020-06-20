local fileutil = require("util.file")
local serpent = require("serpent")
local json = require("json")
local System = require("System")
local fs = require("fs")
local util = require("util")
local Error = require("Error")

local ProjectSettingsFields = {
	audioRouting = AudioChannelRouting,
	midiRouting = MidiChannelRouting,
	layout = InstanceLayout,
	saveType = SaveStateType,
	"zoom"
}

local SystemSettingsFields = {
	emulatorType = EmulatorType,
	"romPath",
	"sramPath"
}

local SameBoySettingsFields = {
	model = GameboyModel,
	"gameLink"
}

local PROJECT_LUA_FILENAME = "project.lua"

local function cloneFields(source, fields, target)
	if target == nil then
		target = {}
	end

	for _, v in ipairs(fields) do
		target[v] = source[v]
	end

	return target
end

local function toEnumString(enumType, value)
	local idx = getmetatable(enumType).__index
	for k, v in pairs(idx) do
		if value == v then return k end
	end
end

local function fromEnumString(enumType, value)
	local v = enumType[value]
	if v ~= nil then
		return v
	end

	local vl = value:sub(1, 1):upper() .. value:sub(2)
	return enumType[vl]
end

local function cloneEnumFields(obj, fields, target)
	if target == nil then target = {} end
	for k, v in pairs(fields) do
		if type(k) == "number" then
			target[v] = obj[v]
		else
			target[k] = toEnumString(v, obj[k])
		end
	end

	return target
end

local function copyStringFields(obj, fields, target)
	if target == nil then target = {} end
	for k, v in pairs(fields) do
		if type(k) == "number" then
			target[v] = obj[v]
		else
			target[k] = fromEnumString(v, obj[k])
		end
	end

	return target
end

local function validateAndUpgradeProject(projectData)

end

local function loadProject_rp010(projectData)
	local proj = _proxy:getProject()
	copyStringFields(projectData, ProjectSettingsFields, proj.settings)

	_proxy:updateSettings()

	for _, inst in ipairs(projectData.instances) do
		local desc = _proxy:createInstance()
		desc.idx = -1
		desc.fastBoot = true
		copyStringFields(inst, SystemSettingsFields, desc)
		copyStringFields(inst.settings.gameBoy, SameBoySettingsFields, desc.sameBoySettings)
		desc.savPath = inst.lastSramPath

		local romFile = fs.load(inst.romPath, false)
		if romFile ~= nil then
			desc.sourceRomData = romFile.data
			local state = base64.decodeBuffer(inst.state.data)

			if proj.settings.saveType == SaveStateType.Sram then
				desc.sourceSavData = state
			elseif proj.settings.saveType == SaveStateType.State then
				desc.sourceStateData = state
			end
		end

		desc.audioComponentState = serpent.dump(inst.audioComponents)
		_loadRom(desc)
	end
end

local function loadProject_100(projectData)
	local proj = _proxy:getProject()
	copyStringFields(projectData.settings, ProjectSettingsFields, proj.settings)
	_proxy:updateSettings()

	for _, inst in ipairs(projectData.instances) do
		local desc = _proxy:createInstance()
		desc.idx = -1
		desc.fastBoot = true
		copyStringFields(inst, SystemSettingsFields, desc)
		copyStringFields(inst.sameBoy, SameBoySettingsFields, desc.sameBoySettings)

		local romFile = fs.load(inst.romPath, false)
		if romFile ~= nil then
			desc.sourceRomData = romFile
			local state = base64.decodeBuffer(inst.state)

			if proj.settings.saveType == SaveStateType.Sram then
				desc.sourceSavData = state
			elseif proj.settings.saveType == SaveStateType.State then
				desc.sourceStateData = state
			end
		end

		desc.audioComponentState = serpent.dump(inst.audioComponents)
		_loadRom(desc)
	end
end

local function zipEntryExists(entries, name)
	for i, v in ipairs(entries) do
		if v.name == name then return true end
	end

	return false
end

local function createProjectSystems(projectData, zip)
	local systems = {}
	for i, inst in ipairs(projectData.systems) do
		local system = SystemDesc.new()
		system.idx = -1
		system.fastBoot = true
		copyStringFields(inst, SystemSettingsFields, system)
		copyStringFields(inst.sameBoy, SameBoySettingsFields, system.sameBoySettings)

		local romData

		if zip ~= nil then
			romData = zip:read(tostring(i) .. ".gb")
		end

		if not isNullPtr(romData) then
			system.state = SystemState.Initialized
			system.sourceRomData = romData
			system.romName = util.getRomName(romData)
		else
			local romFile = fs.load(inst.romPath, false)
			if romFile ~= nil then
				system.state = SystemState.Initialized
				system.sourceRomData = romFile
				system.romName = util.getRomName(romFile)
			else
				system.state = SystemState.RomMissing
			end
		end

		local saveType = fromEnumString(SaveStateType, projectData.settings.saveType)

		if saveType == SaveStateType.State then
			-- TODO: Check SameBoy version here and determine whether the state is still valid
			-- If it isn't prompt the user if they want to use the SRAM data instead
			local stateData = zip:read(tostring(i) .. ".state")
			if not isNullPtr(stateData) then
				system.sourceStateData = stateData
			else
				print("WARNING: Couldn't find state data")
			end
		else
			local sramData = zip:read(tostring(i) .. ".sav")
			if not isNullPtr(sramData) then
				system.sourceSavData = sramData
			else
				print("WARNING: Couldn't find SRAM data")
			end
		end

		system.audioComponentState = serpent.dump(inst.audioComponents)
		system.uiComponentState = serpent.dump(inst.uiComponents)

		table.insert(systems, System(system))
	end

	return systems
end

local function loadProject(data)
	local projectData
	local zip = ZipReader.new(data)
	if zip:isValid() then
		local entries = zip:entries()
		if zipEntryExists(entries, PROJECT_LUA_FILENAME) then
			local fileData = zip:read(PROJECT_LUA_FILENAME)
			local ok, loadedData = serpent.load(fileData:toString())
			if ok == false then
				zip:close()
				return nil, nil, Error("Failed to load project: Unable to parse lua project")
			end

			projectData = loadedData
		else
			return nil, nil, Error("Failed to load project: Project file missing")
		end
	else
		local fileData, err = fileutil.loadPathOrData(data)
		if err ~= nil then return nil, nil, err end

		-- Old projects (<= v0.2.0) are encoded using JSON rather than lua
		local stringData = fileData:toString()
		projectData = json.decode(stringData)
		if projectData == nil then
			return nil, nil, Error("Failed to load project: Unable to deserialize file")
		end
	end

	local err = validateAndUpgradeProject(projectData)
	if err ~= nil then return nil, nil, err end

	local systems, err = createProjectSystems(projectData, zip)
	if err ~= nil then return nil, nil, err end

	zip:close()

	return projectData, systems, nil
end

local function saveProject(path, projectData, systems, systemStates, zipSettings)
	local ok

	local zip
	if type(path) == "string" and path ~= "" then
		zip = ZipWriter.new(path, zipSettings)
		if not zip:isValid() then return Error("Failed to open output file") end
	else
		zip = ZipWriter.new(zipSettings)
	end

	ok = zip:add(PROJECT_LUA_FILENAME, projectData)
	if ok == false then return Error("Failed to add project config") end

	for i, system in ipairs(systems) do
		if systemStates.srams[i] ~= nil then
			ok = zip:add(tostring(i) .. ".sav", systemStates.srams[i])
			if ok == false then return Error("Failed to add system SRAM") end

			ok = zip:add(tostring(i) .. ".state", systemStates.states[i])
			if ok == false then return Error("Failed to add system state") end
		end

		ok = zip:add(tostring(i) .. ".gb", system.desc.sourceRomData)
		if ok == false then return Error("Failed to add system ROM") end
	end

	zip:close()

	if type(path) == "userdata" then
		zip:copyTo(path)
	end

	zip:free()
end

return {
	loadProject = loadProject,
	copyStringFields = copyStringFields,
	cloneEnumFields = cloneEnumFields,
	ProjectSettingsFields = ProjectSettingsFields,
	SystemSettingsFields = SystemSettingsFields,
	SameBoySettingsFields = SameBoySettingsFields,
	saveProject = saveProject,
	createProjectSystems = createProjectSystems
}
