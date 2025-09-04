#include "WebAudioManager.h"

#ifdef FW_PLATFORM_WEB

#include <spdlog/spdlog.h>
#include <emscripten.h>
#include <emscripten/em_math.h>
#include <emscripten/webaudio.h>

uint8_t audioThreadStack[4096];

EM_BOOL generateAudio(int numInputs, const AudioSampleFrame *inputs,
                      int numOutputs, AudioSampleFrame *outputs,
                      int numParams, const AudioParamFrame *params,
                      void *userData)
{

	/*for(int i = 0; i < numOutputs; ++i) {
		for(int j = 0; j < 128*outputs[i].numberOfChannels; ++j) {
			outputs[i].data[j] = emscripten_random() * 0.2 - 0.1; // Warning: scale down audio volume by factor of 0.2, raw noise can be really loud otherwise
		}
	}*/

	assert(userData);
	fw::audio::WebAudioManager* manager = reinterpret_cast<fw::audio::WebAudioManager*>(userData);
	fw::StereoAudioBuffer& input = manager->getInput();
	fw::StereoAudioBuffer& output = manager->getOutput();
	fw::AudioProcessorPtr processor = manager->getProcessor();
	assert(processor);

	if (processor) {
		input.clear();

		processor->onBeginUpdate(128);
		processor->onRender(output.getSamples(), input.getSamples(), 128);

		assert(numOutputs == 1);
		assert(outputs[0].numberOfChannels == 2);

		for (uint32 i = 0; i < output.ChannelCount; ++i) {
			for (uint32 j = 0; j < output.getFrameCount(); ++j) {
				outputs[0].data[i * output.getFrameCount() + j] = output.getSample(j, i);
			}
		}

		/*for (uint32 i = 0; i < output.getFrameCount(); ++i) {
			for (uint32 j = 0; j < output.ChannelCount; ++j) {
				outputs[0].data[j * output.getFrameCount() + i] = output.getSample(i, j);
			}
		}*/
	}

	return EM_TRUE; // Keep the graph output going
}

void audioWorkletProcessorCreated(EMSCRIPTEN_WEBAUDIO_T audioContext, EM_BOOL success, void *userData) {
	if (!success) return; // Check browser console in a debug build for detailed errors
	assert(userData);

	int outputChannelCounts[1] = { 2 };
	EmscriptenAudioWorkletNodeCreateOptions options = {
		.numberOfInputs = 0,
		.numberOfOutputs = 1,
		.outputChannelCounts = outputChannelCounts
	};

	// Create node
	EMSCRIPTEN_AUDIO_WORKLET_NODE_T wasmAudioWorklet = emscripten_create_wasm_audio_worklet_node(
		audioContext,
    	"framework-generator",
		&options,
		&generateAudio,
		userData
	);

	// Connect it to audio context destination
	emscripten_audio_node_connect(wasmAudioWorklet, audioContext, 0, 0);
}

void audioThreadInitialized(EMSCRIPTEN_WEBAUDIO_T audioContext, EM_BOOL success, void* userData) {
	if (!success) return; // Check browser console in a debug build for detailed errors
	assert(userData);

	WebAudioWorkletProcessorCreateOptions opts = {
		.name = "framework-generator",
	};

	emscripten_create_wasm_audio_worklet_processor_async(audioContext, &opts, &audioWorkletProcessorCreated, userData);
}

namespace fw::audio {
	WebAudioManager::WebAudioManager(int audioContextId): _audioContextId(audioContextId) {
		_output.resize(128);
	}

	WebAudioManager::~WebAudioManager() {
		stop();
	}

	bool WebAudioManager::setAudioDevice(int32 idx) {
		return true;
	}

	bool WebAudioManager::loadFile(std::string_view path, std::vector<f32>& target) {
		return false;
	}

	bool WebAudioManager::start(int32 idx) {
		if (_running) return false;

		//EMSCRIPTEN_WEBAUDIO_T context = emscripten_create_audio_context(0);
		emscripten_start_wasm_audio_worklet_thread_async(_audioContextId, audioThreadStack, sizeof(audioThreadStack),
														 &audioThreadInitialized, this);

		_running = true;
		return true;
	}

	void WebAudioManager::stop() {

	}

	f32 WebAudioManager::getSampleRate() {
		return 48000;
	}
}

#endif
