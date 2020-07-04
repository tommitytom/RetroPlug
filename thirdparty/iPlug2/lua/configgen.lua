local plugin = {
	version = "0.3.0",
	name = "RetroPlug",
	author = "tommitytom",
	uniqueId = "2wvF",
	authorId = "tmtt",
	url = "tommitytom.co.uk",
	email = "retroplug@tommitytom.co.uk",
	copyright = "Copyright 2020 Tom Yaxley"
}

local configtool = {}

local semver = require("src.scripts.common.util.semver")

local function interp(s, tab)
	return (s:gsub('($%b{})', function(w)
		return tab[w:sub(3, -2)] or w
	end))
end

local function copyFields(target, source)
	for k, v in pairs(source) do
		target[k] = v
	end
end

local function convertBools(tab)
	for k, v in pairs(tab) do
		if type(v) == "boolean" then
			if v == true then tab[k] = 1 else tab[k] = 0 end
		end
	end
end

function configtool.generate(settings)
	local v = semver(settings.plugin.version)
	local data = {
		versionHex = string.format("0x%.8x", (v.major << 16) | (v.minor << 8) | (v.patch & 0xFF)),
		channelIo = tostring(settings.iplug.config.inputs) .. "-" .. tostring(settings.iplug.config.outputs)
	}

	copyFields(data, settings.plugin)
	copyFields(data, settings.iplug.config)
	convertBools(data)

	local s = interp([[// !! WARNING - THIS FILE IS GENERATED !!
#pragma once

#define PLUG_NAME "${name}"
#define PLUG_MFR "${author}"
#define PLUG_VERSION_HEX ${versionHex}
#define PLUG_VERSION_STR "${version}"
#define PLUG_UNIQUE_ID '${uniqueId}'
#define PLUG_MFR_ID '${authorId}'
#define PLUG_URL_STR "${url}"
#define PLUG_EMAIL_STR "${email}"
#define PLUG_COPYRIGHT_STR "${copyright}"
#define PLUG_CLASS_NAME ${name}Instrument

#define BUNDLE_NAME "${name}"
#define BUNDLE_MFR "${author}"
#define BUNDLE_DOMAIN "com"

#define PLUG_CHANNEL_IO "${channelIo}"

#define PLUG_LATENCY ${latency}
#define PLUG_TYPE 1
#define PLUG_DOES_MIDI_IN ${midiIn}
#define PLUG_DOES_MIDI_OUT ${midiOut}
#define PLUG_DOES_MPE 1
#define PLUG_DOES_STATE_CHUNKS ${stateChunks}
#define PLUG_HAS_UI ${ui}
#define PLUG_WIDTH ${width}
#define PLUG_HEIGHT ${height}
#define PLUG_FPS ${fps}
#define PLUG_SHARED_RESOURCES ${sharedResources}

#define AUV2_ENTRY ${name}_Entry
#define AUV2_ENTRY_STR "${name}_Entry"
#define AUV2_FACTORY ${name}_Factory
#define AUV2_VIEW_CLASS ${name}_View
#define AUV2_VIEW_CLASS_STR "${name}_View"

#define AAX_TYPE_IDS 'EFN1', 'EFN2'
#define AAX_PLUG_MFR_STR "${author}"
#define AAX_PLUG_NAME_STR "${name}\nIPIS"
#define AAX_DOES_AUDIOSUITE 0
#define AAX_PLUG_CATEGORY_STR "Synth"

#define VST3_SUBCATEGORY "Instrument"

#define APP_NUM_CHANNELS 2
#define APP_N_VECTOR_WAIT 0
#define APP_MULT 1
#define APP_COPY_AUV3 0
#define APP_RESIZABLE 0
#define APP_SIGNAL_VECTOR_SIZE 64
	]], data)

	print("Writing config to " .. settings.iplug.config.path)
	local file = io.open(settings.iplug.config.path, "w")
	file:write(s)
	file:close()
end

return configtool
