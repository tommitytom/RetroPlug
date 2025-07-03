local paths = dofile("../paths.lua")

local VST3_DEP_PATH = paths.DEP_ROOT .. "vst3/"

local m = {}

function m.include()
	externalincludedirs { VST3_DEP_PATH }
end

function m.source()
	m.include()

	filter { "platforms:not Emscripten" }
		files {
            VST3_DEP_PATH .. "base/**.h",
            VST3_DEP_PATH .. "base/**.cpp",
            VST3_DEP_PATH .. "pluginterfaces/base/**.h",
            VST3_DEP_PATH .. "pluginterfaces/base/**.cpp",
            --VST3_DEP_PATH .. "public.sdk/source/vst3stdsdk.cpp",
            VST3_DEP_PATH .. "public.sdk/source/common/commoniids.cpp",
            VST3_DEP_PATH .. "public.sdk/source/common/memorystream.*",
            VST3_DEP_PATH .. "public.sdk/source/common/pluginview.*",
            VST3_DEP_PATH .. "public.sdk/source/common/commonstringconvert.*",
            VST3_DEP_PATH .. "public.sdk/source/common/threadchecker.*",
            VST3_DEP_PATH .. "public.sdk/source/main/pluginfactory.*",
            VST3_DEP_PATH .. "public.sdk/source/vst/utility/dataexchange.*",
            VST3_DEP_PATH .. "public.sdk/source/vst/utility/systemtime.*",
            VST3_DEP_PATH .. "public.sdk/source/vst/utility/stringconvert.*",
            VST3_DEP_PATH .. "public.sdk/source/vst/utility/testing.*",
            VST3_DEP_PATH .. "public.sdk/source/vst/utility/vst2persistence.*",
            VST3_DEP_PATH .. "public.sdk/source/vst/vstaudioeffect.*",
            VST3_DEP_PATH .. "public.sdk/source/vst/vstbus.*",
            VST3_DEP_PATH .. "public.sdk/source/vst/vstcomponent.*",
            VST3_DEP_PATH .. "public.sdk/source/vst/vstcomponentbase.*",
            --VST3_DEP_PATH .. "public.sdk/source/vst/vsteditcontroller.*",
            VST3_DEP_PATH .. "public.sdk/source/vst/vstinitiids.cpp",
            VST3_DEP_PATH .. "public.sdk/source/vst/vstnoteexpressiontypes.cpp",
            VST3_DEP_PATH .. "public.sdk/source/vst/vstparameters.*",
            VST3_DEP_PATH .. "public.sdk/source/vst/vstpresentation.*",
            VST3_DEP_PATH .. "public.sdk/source/vst/vstsinglecomponenteffect.*",

            --[[VST3_DEP_PATH .. "public.sdk/source/vst/hosting/connectionproxy.*",
            VST3_DEP_PATH .. "public.sdk/source/vst/hosting/eventlist.*",
            VST3_DEP_PATH .. "public.sdk/source/vst/hosting/hostclasses.*",
            VST3_DEP_PATH .. "public.sdk/source/vst/hosting/module.*",
            VST3_DEP_PATH .. "public.sdk/source/vst/hosting/parameterchanges.*",
            VST3_DEP_PATH .. "public.sdk/source/vst/hosting/plugprovider.*",
            VST3_DEP_PATH .. "public.sdk/source/vst/hosting/pluginterfacesupport.*",
            VST3_DEP_PATH .. "public.sdk/source/vst/hosting/processdata.*",]]
        }

	filter{}
end

function m.link()
	m.include()
	links { "vst3" }
end

function m.project()
	project "vst3"
		kind "StaticLib"

		m.source()
end

return m
