#include "KitUtil.h"

#include <chrono>

#include <spdlog/spdlog.h>

#define MA_LOG_LEVEL MA_LOG_LEVEL_VERBOSE
#include <miniaudio/miniaudio.h>

#include <r8brain/r8bbase.h>
#include <r8brain/CDSPResampler.h>

using namespace rp;

const uint32 GAMEBOY_SAMPLE_RATE = 11468;

KitUtil::SampleData KitUtil::loadSample(std::string_view path) {
	ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 1, 0);

	ma_decoder decoder;
	ma_result result = ma_decoder_init_file(path.data(), &config, &decoder);

	if (result != MA_SUCCESS) {
		return KitUtil::SampleData{};
	}

	size_t blockSize = 24000;
	size_t offset = 0;

	KitUtil::SampleData sample;
	sample.sampleRate = decoder.internalSampleRate;
	sample.buffer = std::make_shared<Float32Buffer>();

	while (true) {
		sample.buffer->resize(sample.buffer->size() + blockSize);

		ma_uint64 framesRead = ma_decoder_read_pcm_frames(&decoder, sample.buffer->data() + offset, blockSize);
		offset += (size_t)framesRead;

		if (framesRead < blockSize) {
			sample.buffer->resize(offset);
			break;
		}
	}

	return sample;
}

struct BiquadCoeffs {
	f32 b0;
	f32 b1;
	f32 b2;
	f32 a0;
	f32 a1;
	f32 a2;
};

const f32 PI = 3.14159265359f;

void lowPassCoeffs(f32 cutoff, f32 q, f32 sampleRate, BiquadCoeffs& target) {
	f32 omega = 2.0f * PI * cutoff / sampleRate;
	f32 s = sin(omega);
	f32 c = cos(omega);
	f32 alpha = s / (2 * q);

	target.b0 = (1.0f - c) / 2.0f;
	target.b1 = 1.0f - c;
	target.b2 = (1.0f - c) / 2.0f;
	target.a0 = 1.0f + alpha;
	target.a1 = -2.0f * c;
	target.a2 = 1.0f - alpha;
}

const f32 SAMPLE_RATE = 11468.0f;
const f32 NYQUIST = SAMPLE_RATE / 2.0f;

const f32 CUTOFF_MAX = NYQUIST;

const f32 Q_MIN = 0.001f;
const f32 Q_MAX = 1.0f;
const f32 Q_RANGE = Q_MAX - Q_MIN;

void writeString(uint8* target, size_t targetSize, std::string_view source, char startVal) {
	memset(target, startVal, targetSize);
	memcpy(target, source.data(), std::min(targetSize, source.size()));
}

enum FilterType {
	NONE, 
	LOWP, 
	HIGHP, 
	BANDP, 
	ALLP
};

void convertSamplerate(f64 inputSampleRate, f64 outputSampleRate, const Float32Buffer& buffer, Float32Buffer& target) {
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

void KitUtil::patchKit(lsdj::Kit& kit, KitState& kitState, const std::vector<SampleData>& samples) {
	auto startTime = std::chrono::high_resolution_clock::now();

	assert(kitState.samples.size() == samples.size());
	assert(samples.size() < 16);

	std::vector<Uint8BufferPtr> targets;
	uint32 totalSampleDataSize = 0;

	for (size_t i = 0; i < samples.size(); ++i) {
		const SampleData& sample = samples[i];
		SampleSettings settings = kitState.samples[i].settings;

		if (settings.cutoff == -1) settings.cutoff = kitState.settings.cutoff;
		if (settings.dither == -1) settings.dither = kitState.settings.dither;
		if (settings.filter == -1) settings.filter = kitState.settings.filter;
		if (settings.pitch == -1) settings.pitch = kitState.settings.pitch;
		if (settings.q == -1) settings.q = kitState.settings.q;
		if (settings.volume == -1) settings.volume = kitState.settings.volume;
		if (settings.gain == -1) settings.gain = kitState.settings.gain;

		// Normalize and Apply gain

		Float32Buffer gainTarget(sample.buffer->size());

		f32 max = 0.0f;
		for (size_t i = 0; i < gainTarget.size(); ++i) {
			f32 value = fabs(sample.buffer->get(i));
			if (value > max) {
				max = value;
			}
		}

		std::array<f32, 7> GAIN_MULTIPLIER_LOOKUP = { 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 3.5f, 4.0f };
		
		f32 normalizeGain = 1.0f / max;
		f32 volumeGain = (f32)settings.volume / (f32)0xFF;
		f32 gainMultiplier = GAIN_MULTIPLIER_LOOKUP[settings.gain >= 0 && settings.gain < GAIN_MULTIPLIER_LOOKUP.size() ? settings.gain : 0];

		for (size_t i = 0; i < gainTarget.size(); ++i) {
			gainTarget.set(i, sample.buffer->get(i) * volumeGain * normalizeGain * gainMultiplier);
		}

		// Apply filter

		Float32Buffer filterTarget = gainTarget.clone();

		//if (settings.filter != FilterType::NONE) {
		if (settings.filter == FilterType::LOWP) {
			f32 cutoff = settings.cutoff / 255.0f;
			f32 q = settings.q / 255.0f;

			cutoff *= CUTOFF_MAX;
			q = q * Q_RANGE + Q_MIN;

			BiquadCoeffs coeff;

			switch (settings.filter) {
			case FilterType::LOWP: 
				lowPassCoeffs(cutoff, q, SAMPLE_RATE, coeff);
				break;
			case FilterType::HIGHP:
				//lowPassCoeffs(cutoff, q, SAMPLE_RATE, coeff);
				break;
			case FilterType::BANDP:
				//lowPassCoeffs(cutoff, q, SAMPLE_RATE, coeff);
				break;
			case FilterType::ALLP:
				//lowPassCoeffs(cutoff, q, SAMPLE_RATE, coeff);
				break;
			}

			ma_biquad filter;
			ma_biquad_config filterConfig = ma_biquad_config_init(ma_format_f32, 1, coeff.b0, coeff.b1, coeff.b2, coeff.a0, coeff.a1, coeff.a2);
			ma_result filterResult = ma_biquad_init(&filterConfig, &filter);
			if (filterResult == MA_SUCCESS) {
				ma_biquad_process_pcm_frames(&filter, filterTarget.data(), gainTarget.data(), gainTarget.size());
			} else {
				spdlog::warn("Failed to create filter");
			}
		}

		// Resample
		Float32Buffer resampled;
		convertSamplerate((f64)sample.sampleRate, (f64)GAMEBOY_SAMPLE_RATE, filterTarget, resampled);

		/*ma_resampler_config config = ma_resampler_config_init(ma_format_f32, 1, sample.sampleRate, GAMEBOY_SAMPLE_RATE, ma_resample_algorithm_linear);
		ma_resampler resampler;
		ma_result resamplerResult = ma_resampler_init(&config, &resampler);
		if (resamplerResult != MA_SUCCESS) {
			spdlog::error("Failed to initialize resampler");
			continue;
		}

		ma_uint64 frameCountIn = filterTarget.size();
		ma_uint64 frameCountOut = ma_resampler_get_expected_output_frame_count(&resampler, frameCountIn);

		ma_result result = ma_resampler_process_pcm_frames(&resampler, filterTarget.data(), &frameCountIn, resampled.data(), &frameCountOut);
		if (result != MA_SUCCESS) {
			spdlog::error("Failed to resample");
			continue;
		}

		ma_resampler_uninit(&resampler);*/

		// Convert to nibbles

		const f32 maxDither = 0.125f;
		f32 ditherLevel = 0.0f;
		if (settings.dither > 0) {
			ditherLevel = ((f32)settings.dither / (f32)0xFF) * maxDither;
		}

		Uint8BufferPtr sampleData = std::make_shared<Uint8Buffer>();
		lsdj::SampleUtil::convertF32ToNibbles(resampled, *sampleData, ditherLevel);

		targets.push_back(sampleData);
		totalSampleDataSize += (uint32)sampleData->size();
	}

	assert(totalSampleDataSize <= lsdj::Kit::MAX_SAMPLE_SPACE);

	Uint8Buffer kitData(lsdj::Rom::BANK_SIZE);
	kitData.clear();
	writeString(kitData.data() + lsdj::Kit::NAME_OFFSET, lsdj::Kit::NAME_SIZE, kitState.name, ' ');

	uint16* offsets = (uint16*)kitData.data();
	uint8* names = kitData.data() + lsdj::Kit::SAMPLE_NAME_OFFSET;
	uint8* sampleData = kitData.data() + lsdj::Kit::SAMPLE_DATA_OFFSET;
	uint16 sampleDataOffset = 0x4000 + (uint16)lsdj::Kit::SAMPLE_DATA_OFFSET;
	
	uint16 offset = 0;
	offsets[0] = sampleDataOffset;

	for (size_t i = 0; i < lsdj::Kit::MAX_SAMPLES; ++i) {
		if (i < targets.size()) {
			// Write name
			writeString(names + lsdj::Kit::SAMPLE_NAME_SIZE * i, lsdj::Kit::SAMPLE_NAME_SIZE, kitState.samples[i].name, '-');

			// Write sample
			memcpy(sampleData + offset, targets[i]->data(), targets[i]->size());

			// Write offset
			offset += (uint16)targets[i]->size();
			offsets[i + 1] = offset + sampleDataOffset;
		} else {
			memset(names + lsdj::Kit::SAMPLE_NAME_SIZE * i, '-', lsdj::Kit::SAMPLE_NAME_SIZE);
			offsets[i + 1] = 0;
		}		
	}

	kit.kitData.write(0, kitData);

	auto endTime = std::chrono::high_resolution_clock::now();

	std::chrono::duration<double, std::milli> fp_ms = endTime - startTime;

	//spdlog::info("sample processing time: {}", fp_ms.count());
}
