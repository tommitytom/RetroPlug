local projectutil = require("util.project")
local serpent = require("serpent")

local Serializer = {}

function Serializer.serializeProject(systems, audioSystemStates, projectSettings, pretty)
	local t = {
		retroPlugVersion = _RETROPLUG_VERSION,
		projectVersion = _PROJECT_VERSION,
		path = projectSettings.path,
		settings = projectutil.cloneEnumFields(projectSettings.settings, projectutil.ProjectSettingsFields),
		systems = {}
	}

	for i, system in ipairs(systems) do
		local desc = system.desc
		if desc.state ~= SystemState.Uninitialized then
			local inst = projectutil.cloneEnumFields(desc, projectutil.SystemSettingsFields)
			inst.sameBoy = projectutil.cloneEnumFields(desc.sameBoySettings, projectutil.SameBoySettingsFields)
			inst.uiComponents = system.state
			inst.audioComponents = {}

			inst.input = {
				key = system.inputMap.key.filename,
				pad = system.inputMap.pad.filename
			}

			local ok, audioComponents = serpent.load(audioSystemStates.components[i])
			if ok == true and audioComponents ~= nil then
				inst.audioComponents = audioComponents
			end

			table.insert(t.systems, inst)
		else
			break
		end
	end

	local opts = { comment = false }
	if pretty == true then opts.indent = '\t' end
	return serpent.block(t, opts)
end

return Serializer
