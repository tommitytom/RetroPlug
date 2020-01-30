require("component")
require("constants")
require("Action")
require("Print")
local cm = require("ComponentManager")

local MAX_INSTANCES = 4
local _instances = {}

function KeyMap() end
function GlobalKeyMap() end
function PadMap() end
function GlobalPadMap() end

function _loadComponent(name)
    cm.loadComponent(name)
end

function _init()
    cm.createGlobalComponents()

	for i = 1, MAX_INSTANCES, 1 do
        local inst = _model:getInstance(i - 1)
        if inst ~= nil then
            local desc = inst:getDesc()
			local instance = {
				model = inst,
				components = cm.createComponents(desc, inst)
			}

			table.insert(_instances, instance)
		end
	end

	for _, instance in ipairs(_instances) do
		cm.runAllHandlers("onComponentsInitialized", instance.components, instance.components)
		cm.runAllHandlers("onReload", instance.components, instance.model)
	end
end

local function processMidiMessage(inst, msg)
    cm.runComponentHandlers("onMidi", inst.components, msg)
end

function _onMidi(offset, status, data1, data2)
    local channel = status & 0x0F

    local r = _model:getSettings().midiRouting
    if r == MidiChannelRouting.SendToAll then
        local msg = { offset = offset, status = status, data1 = data1, data2 = data2 }
        for _, v in ipairs(_instances) do
            processMidiMessage(v, msg)
        end
    elseif r == MidiChannelRouting.OneChannelPerInstance then
        local target = _instances[channel]
        if target ~= nil then
            local msg = { offset = offset, status = status, data1 = data1, data2 = data2 }
            processMidiMessage(target, msg)
        end
    elseif r == MidiChannelRouting.FourChannelsPerInstance then
        local targetIdx = math.floor(channel / 4)
        local target = _instances[targetIdx + 1]
        if target ~= nil then
            local msg = { offset = offset, status = (channel | (status << 4)), data1 = data1, data2 = data2 }
            processMidiMessage(target, msg)
        end
    end
end
