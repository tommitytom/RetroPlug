#include "KitUtil.h"

#include <spdlog/spdlog.h>

#define MA_LOG_LEVEL MA_LOG_LEVEL_VERBOSE
#include <miniaudio/miniaudio.h>

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

void KitUtil::patchKit(lsdj::Kit& kit, KitState& kitState, const std::vector<SampleData>& samples) {
	assert(kitState.samples.size() == samples.size());
	assert(samples.size() < 16);

	std::vector<Uint8BufferPtr> targets;
	uint32 totalSampleDataSize = 0;

	for (size_t i = 0; i < samples.size(); ++i) {
		const SampleData& sample = samples[i];
		const SampleSettings& settings = kitState.samples[i].settings;

		// Apply gain

		Float32Buffer gainTarget(sample.buffer->size());
		f32 gain = (f32)settings.volume / (f32)0xFF;

		for (size_t i = 0; i < gainTarget.size(); ++i) {
			gainTarget.set(i, sample.buffer->get(i) * gain);
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

		ma_resampler_config config = ma_resampler_config_init(ma_format_f32, 1, sample.sampleRate, GAMEBOY_SAMPLE_RATE, ma_resample_algorithm_linear);
		ma_resampler resampler;
		ma_result resamplerResult = ma_resampler_init(&config, &resampler);
		if (resamplerResult != MA_SUCCESS) {
			spdlog::error("Failed to initialize resampler");
			continue;
		}

		ma_uint64 frameCountIn = filterTarget.size();
		ma_uint64 frameCountOut = ma_resampler_get_expected_output_frame_count(&resampler, frameCountIn);

		Float32Buffer resampled((size_t)frameCountOut);

		ma_result result = ma_resampler_process_pcm_frames(&resampler, filterTarget.data(), &frameCountIn, resampled.data(), &frameCountOut);
		if (result != MA_SUCCESS) {
			spdlog::error("Failed to resample");
			continue;
		}

		ma_resampler_uninit(&resampler);

		// Convert to nibbles

		Uint8BufferPtr sampleData = std::make_shared<Uint8Buffer>();
		lsdj::SampleUtil::convertF32ToNibbles(resampled, *sampleData);

		targets.push_back(sampleData);
		totalSampleDataSize += sampleData->size();
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
}
