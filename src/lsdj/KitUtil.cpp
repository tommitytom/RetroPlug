#include "KitUtil.h"

#include <chrono>

#include <spdlog/spdlog.h>

#include <r8brain/r8bbase.h>
#include <r8brain/CDSPResampler.h>

#include "foundation/StringUtil.h"
#include "lsdj/SampleUtil.h"
#include "util/GameboyUtil.h"
#include "ecs/Effects.h"

using namespace rp;

void KitUtil::convertSamplerate(f64 inputSampleRate, f64 outputSampleRate, const fw::Float32Buffer& buffer, fw::Float32Buffer& target) {
	const size_t inBufCapacity = 1024;
	r8b::CFixedBuffer<f64> inBuf;
	inBuf.alloc((int)buffer.size());

	r8b::CPtrKeeper<r8b::CDSPResampler24*> resampler = new r8b::CDSPResampler24(inputSampleRate, outputSampleRate, (int)buffer.size());
	size_t minInputSize = (size_t)resampler->getInLenBeforeOutStart();

	size_t targetSize = (size_t)(buffer.size() * (outputSampleRate / inputSampleRate));
	target.resize(targetSize);

	size_t sourcePos = 0;
	size_t targetPos = 0;

	while (targetPos < targetSize) {
		memset(inBuf.getPtr(), 0, inBufCapacity * sizeof(f64));

		size_t chunkSize = std::min(inBufCapacity, buffer.size() - sourcePos);
		for (size_t i = 0; i < chunkSize; ++i) {
			inBuf[i] = (f64)buffer[sourcePos++];
		}

		f64* targetBuffer;
		size_t writeCount = (size_t)resampler->process(inBuf.getPtr(), (int)inBufCapacity, targetBuffer);

		if (targetPos + writeCount > targetSize) {
			writeCount = targetSize - targetPos;
		}

		for (size_t i = 0; i < writeCount; ++i) {
			target[targetPos++] = (f32)targetBuffer[i];
		}
	}
}

bool processSamples(SampleCache& sampleCache, const LsdjEditableKit& kit, std::vector<std::pair<std::string, fw::Uint8Buffer>>& samples) {
	for (const LsdjSampleComponent& sample : kit.samples) {
		const SampleData* sampleDataRaw = sampleCache.getOrLoadSample(sample.path);
		if (!sampleDataRaw) {
			spdlog::error("Failed to load sample: {} from path: {}", sample.name, sample.path);
			continue;
		}

		const f32 srRatio = (f32)sampleDataRaw->sampleRate / (f32)GameboyUtil::GAMEBOY_SAMPLE_RATE;
		// NOTE: It's fine if maxFrames is larger than the actual max sample size at the given sample rate
		// since samples get clipped before writing to the kit.
		const size_t maxFrames = (size_t)ceil((f32)lsdj::Kit::MAX_SAMPLE_FRAMES * srRatio);
		const size_t readFrames = std::min(maxFrames, sample.length == 0 ? sampleDataRaw->buffer.size() : sample.length);

		SampleData sampleData{
			.buffer = sampleDataRaw->buffer.slice(sample.offset, readFrames).clone(),
			.sampleRate = sampleDataRaw->sampleRate
		};

		const DitherEffect* ditherEffect = nullptr;
		for (const LsdjEffect& effect : kit.effects) {
			effect.visit([&](auto&& eff) {
				if constexpr (!std::is_same_v<std::decay_t<decltype(eff)>, DitherEffect>) {
					processEffect(eff, sampleData.buffer, (f32)sampleData.sampleRate);
				} else {
					ditherEffect = &eff;
				}
			});
		}

		fw::Float32Buffer resampled;
		KitUtil::convertSamplerate((f64)sampleData.sampleRate, (f64)GameboyUtil::GAMEBOY_SAMPLE_RATE, sampleData.buffer, resampled);

		if (ditherEffect) {
			processEffect(*ditherEffect, resampled, (f32)GameboyUtil::GAMEBOY_SAMPLE_RATE);
		} else {
			f32* resampledData = resampled.data();
			for (size_t i = 0; i < resampled.size(); ++i) {
				// Clamp to [-1, 1], then scale to [0, 15]
				resampledData[i] = (std::clamp(resampledData[i], -1.0f, 1.0f) + 1.0f) * 0.5f;
				resampledData[i] = std::round(resampledData[i] * 15.0f);
			}
		}

		fw::Uint8Buffer data;
		lsdj::SampleUtil::convertScaledF32ToNibbles(resampled, data);

		samples.push_back({ sample.name, std::move(data) });
	}

	return true;
}

bool KitUtil::createKit(SampleCache& sampleCache, lsdj::Kit& kit, const LsdjEditableKit& kitState) {
	std::vector<std::pair<std::string, fw::Uint8Buffer>> samples;
	if (!processSamples(sampleCache, kitState, samples)) {
		return false;
	}

	kit.setName(kitState.name.size() ? kitState.name : "GR8KIT");
	kit.writeSamples(samples);

	return true;
}

std::optional<std::string> KitUtil::updateKit2(const LsdjKitComponent& kitComponent, fw::Uint8Buffer& kitData, SampleCache& sampleCache) {
	if (kitData.isOwnerOfData()) {
		kitData.resize(lsdj::Rom::BANK_SIZE);
	} else {
		assert(kitData.size() == lsdj::Rom::BANK_SIZE);
	}

	lsdj::Kit targetKit(MemoryAccessor(MemoryType::Rom, kitData.ref(), 0), -1);
	std::string error;

	kitComponent.kit.visit(entt::overloaded{
		[&](const LsdjEmptyKit&) {
			// In this case we just erase the kit
			kitData.clear();
		},
		[&](const LsdjRomKit& kit) {
			if (kit.name.has_value()) {
				// This will just rename the existing kit
				// TODO: Ensure the kit we're changing the name of is not empty!
				targetKit.setName(kit.name.value());
			}
		},
		[&](const LsdjPatchedKit& kit) {
			if (!fw::FsUtil::readFile(kit.path, kitData)) {
				error = "Failed to read kit file at " + kit.path;
			} else {
				if (kit.name.has_value()) {
					targetKit.setName(kit.name.value());
				}
			}
		},
		[&](const LsdjEditableKit& kit) {
			if (!KitUtil::createKit(sampleCache, targetKit, kit)) {
				error = "Failed to create kit " + std::to_string(kitComponent.id);
			}
		}
	});

	if (error.empty()) {
		return std::nullopt;
	}

	return error;
}
