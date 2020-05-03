local fileutil = require("util.file")
local serpent = require("serpent")
local json = require("json")
local Error = require("Error")

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
	if err ~= nil then return err end

	local stringData = fileData:toString()
	local ok, projectData = serpent.load(stringData)
	if ok ~= true then
		-- Old projects (<= v0.2.0) are encoded using JSON rather than lua
		projectData = json.decode(stringData)
		if projectData == nil then
			return Error("Failed to load project: Unable to deserialize file")
		end
	end

	err = validateAndUpgradeProject(projectData)
	if err ~= nil then return err end

	return loadProject_100(projectData)
end

return {
	loadProject = loadProject
}
