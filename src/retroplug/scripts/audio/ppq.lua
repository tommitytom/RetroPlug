local module = {}

function module.generatePpq(sampleCount, sampleRate, timeInfo, resolution)
	local samplesPerMs = sampleRate / 1000.0
	local beatLenMs = 60000.0 / timeInfo.tempo
	local beatLenSamples = beatLenMs * samplesPerMs
	local beatLenSamples24 = beatLenSamples / resolution

	local ppq24 = timeInfo.ppqPos * resolution
	local framePpqLen = (sampleCount / beatLenSamples) * resolution

	local nextPpq24 = ppq24 + framePpqLen

	local sync = false
	local offset = 0
	if ppq24 == 0 then
		sync = true
	elseif math.floor(ppq24) ~= math.floor(nextPpq24) then
		sync = true
		local amount = math.ceil(ppq24) - ppq24
		offset = math.floor(beatLenSamples24 * amount)
		if (offset >= sampleCount) then
			print("Overshoot", offset - sampleCount)
			offset = sampleCount - 1
		end
	end

	return sync, offset
end

function module.generatePpq24(sampleCount, sampleRate, timeInfo)
	module.generatePpq(sampleCount, sampleRate, timeInfo, 24)
end

return module
