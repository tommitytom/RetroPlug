local fileutil = require("util.file")
local serpent = require("serpent")
local json = require("json")
local System = require("System")
local fs = require("fs")
local log = require("log")
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
		if value == v then
			return k:sub(1, 1):lower() .. k:sub(2)
		end
	end

	return ""
end

local function fromEnumString(enumType, value)
	local v = enumType[value]
	if v ~= nil then return v end

	local vl = value:sub(1, 1):upper() .. value:sub(2)
	v = enumType[vl]
	if v ~= nil then return v end

	return 0
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
		elseif type(k) == "string" then
			target[k] = fromEnumString(v, obj[k])
		else
			log.warn("Failed to merge string field " .. k)
		end
	end

	return target
end

local projectDefaults = {
	["1.0.0"] = {
		system = {
			romPath = "",
			sramPath = "",
			uiComponents = {},
			audioComponents = {},
			sameBoy = {
				model = "Auto",
				gameLink = false
			}
		},
		project = {
			projectVersion = "0.1.0",
			path = "",
			settings = {
				saveType="Sram",
				audioRouting="StereoMixDown",
				zoom=2,
				midiRouting="SendToAll"
			}
		}
	}
}

local function firstToUpper(str)
	if str ~= nil and type(str) == "string" and str ~= "" then
		return (str:gsub("^%l", string.upper))
	end
end

local function updgrade_json_to_pv100(p)
	local syncModes = { "off", "midiSync", "midiSyncArduinoboy" }
	local systems = {}
	for _, v in ipairs(p.instances) do
		local stateData
		if v.state ~= nil and v.state.data ~= nil and type(v.state.data) == "string" then
			stateData = base64.decodeBuffer(v.state.data)
			log.debug(stateData:size())
		end

		local uiComponents = {}
		local audioComponents = {}
		if v.settings.lsdj then
			local lsdj = v.settings.lsdj
			uiComponents["LSDj"] = {
				keyboardShortcuts = lsdj.keyboardShortcuts
			}

			audioComponents["LSDj Arduinoboy"] = {
				autoPlay = lsdj.autoPlay,
				syncMode = "off"
			}
		end

		table.insert(systems, {
			romPath = v.romPath,
			sramPath = v.lastSramPath,
			sameBoy = {
				model = firstToUpper(v.settings.gameBoy.model),
				gameLink = v.settings.gameBoy.gameLink
			},
			uiComponents = {},
			audioComponents = {},
			_stateData = stateData
		})
	end

	return {
		projectVersion = "1.0.0",
		path = p.lastProjectPath,
		systems = systems,

		settings = {
			saveType = firstToUpper(p.saveType),
			audioRouting = firstToUpper(p.audioRouting),
			zoom = 2, --TODO: Set to user defined defaults in config.lua!
			midiRouting = firstToUpper(p.midiRouting),
			layout = firstToUpper(p.layout)
		}
	}
end

local function validateAndUpgradeProject(projectData)
	local err
	if projectData.instances ~= nil and projectData.version == "0.1.0" then
		projectData, err = updgrade_json_to_pv100(projectData)
	end

	-- TODO: Validate!

	return projectData, err
end

local function zipEntryExists(entries, name)
	for i, v in ipairs(entries) do
		if v.name == name then return true end
	end

	return false
end

local function loadSystemResources(projectData, inst, idx, zip)
	local t = {}
	local idxStr = tostring(idx)
	local st = fromEnumString(SaveStateType, projectData.settings.saveType)

	if zip ~= nil then
		t.rom = zip:read(idxStr .. ".gb")
		t.state = zip:read(idxStr .. ".state")
		t.sram = zip:read(idxStr .. ".sav")
	end

	if inst._stateData ~= nil then
		if st == SaveStateType.Sram then
			t.sram = inst._stateData
		elseif st == SaveStateType.State then
			t.state = inst._stateData
		end
	end

	if t.rom == nil or isNullPtr(t.rom) then
		if fs.exists(inst.romPath) then
			t.rom = fs.load(inst.romPath)
		end
	end

	if t.sram == nil or isNullPtr(t.sram) then
		if fs.exists(inst.sramPath) then
			t.sram = fs.load(inst.sramPath)
		end
	end

	if t.rom == nil or isNullPtr(t.rom) then t.rom = nil; log.warn("Couldn't find ROM") end
	if t.state == nil or isNullPtr(t.state) then t.state = nil; log.warn("Couldn't find state data") end
	if t.sram == nil or isNullPtr(t.sram) then t.sram = nil; log.warn("Couldn't find SRAM data") end

	return t
end

local function createProjectSystems(projectData, zip)
	local systems = {}
	for i, inst in ipairs(projectData.systems) do
		local res = loadSystemResources(projectData, inst, i, zip)

		local system = SystemDesc.new()
		system.idx = -1
		system.fastBoot = true
		copyStringFields(inst, SystemSettingsFields, system)
		copyStringFields(inst.sameBoy, SameBoySettingsFields, system.sameBoySettings)

		if res.rom then
			system.state = SystemState.Initialized
			system.sourceRomData = res.rom
			system.romName = util.getRomName(res.rom)
		else
			system.state = SystemState.RomMissing
		end

		if res.state then system.sourceStateData = res.state end
		if res.sram then system.sourceSavData = res.sram end

		system.audioComponentState = serpent.dump(inst.audioComponents)
		system.uiComponentState = serpent.dump(inst.uiComponents)

		table.insert(systems, System(system))
	end

	return systems
end

local function loadProject(data)
	local projectData
	local err

	local zip = ZipReader.new(data)
	if zip:isValid() then
		local entries = zip:entries()
		if zipEntryExists(entries, PROJECT_LUA_FILENAME) then
			local fileData = zip:read(PROJECT_LUA_FILENAME)
			local ok, loadedData = serpent.load(fileData:toString(), { safe = true })
			if ok == false then
				zip:close()
				return nil, nil, Error("Failed to load project: Unable to parse lua project")
			end

			projectData = loadedData
		else
			return nil, nil, Error("Failed to load project: Project file missing")
		end
	else
		zip = nil

		local fileData, err = fileutil.loadPathOrData(data)
		if err ~= nil then return nil, nil, err end

		-- Old projects (<= v0.2.0) are encoded using JSON rather than lua
		local stringData = fileData:toString()
		projectData = json.decode(stringData)
		if projectData == nil then
			return nil, nil, Error("Failed to load project: Unable to deserialize file")
		end
	end

	projectData, err = validateAndUpgradeProject(projectData)
	if err ~= nil then
		if zip then zip:close() end
		return nil, nil, err
	end

	local systems, err = createProjectSystems(projectData, zip)
	if err ~= nil then
		if zip then zip:close() end
		return nil, nil, err
	end

	if zip then zip:close() end

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
		local idx = tostring(i)
		if systemStates.srams[i] ~= nil then
			ok = zip:add(idx .. ".sav", systemStates.srams[i])
			if ok == false then return Error("Failed to add system SRAM") end

			ok = zip:add(idx .. ".state", systemStates.states[i])
			if ok == false then return Error("Failed to add system state") end
		end

		ok = zip:add(idx .. ".gb", system.desc.sourceRomData)
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
