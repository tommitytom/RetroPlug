local fileutil = require("util.file")
local serpent = require("serpent")
local json = require("json")
local Error = require("Error")

local ProjectSettingsFields = {
	audioRouting = AudioChannelRouting,
	midiRouting = MidiChannelRouting,
	layout = InstanceLayout,
	saveType = SaveStateType,
	"zoom"
}

local InstanceSettingsFields = {
	emulatorType = EmulatorType,
	"romPath",
	"savPath"
}

local SameBoySettingsFields = {
	model = GameboyModel,
	"gameLink"
}

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

local function cloneStringFields(obj, fields, target)
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
	cloneStringFields(projectData, ProjectSettingsFields, proj.settings)

	_proxy:updateSettings()

	for _, inst in ipairs(projectData.instances) do
		local desc = _proxy:createInstance()
		desc.idx = -1
		desc.fastBoot = true
		cloneStringFields(inst, InstanceSettingsFields, desc)
		cloneStringFields(inst.settings.gameBoy, SameBoySettingsFields, desc.sameBoySettings)
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
	cloneStringFields(projectData.settings, ProjectSettingsFields, proj.settings)
	_proxy:updateSettings()

	for _, inst in ipairs(projectData.instances) do
		local desc = _proxy:createInstance()
		desc.idx = -1
		desc.fastBoot = true
		cloneStringFields(inst, InstanceSettingsFields, desc)
		cloneStringFields(inst.sameBoy, SameBoySettingsFields, desc.sameBoySettings)

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

local function loadProject(data)
	local fileData, err = fileutil.loadPathOrData(data)
	if err ~= nil then return nil, err end

	local stringData = fileData:toString()
	local ok, projectData = serpent.load(stringData)
	if ok ~= true then
		-- Old projects (<= v0.2.0) are encoded using JSON rather than lua
		projectData = json.decode(stringData)
		if projectData == nil then
			return nil, Error("Failed to load project: Unable to deserialize file")
		end
	end

	err = validateAndUpgradeProject(projectData)
	if err ~= nil then return nil, err end

	return projectData, nil
end

return {
	loadProject = loadProject,
	cloneStringFields = cloneStringFields,
	ProjectSettingsFields = ProjectSettingsFields,
	InstanceSettingsFields = InstanceSettingsFields,
	SameBoySettingsFields = SameBoySettingsFields
}
