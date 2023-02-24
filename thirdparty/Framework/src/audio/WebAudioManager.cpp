#include "WebAudioManager.h"

#ifdef RP_WEB

#include <emscripten/em_math.h>
#include <emscripten/webaudio.h>

#include <spdlog/spdlog.h>

uint8_t audioThreadStack[4096];

EM_BOOL OnCanvasClick(int eventType, const EmscriptenMouseEvent *mouseEvent, void *userData) {
	EMSCRIPTEN_WEBAUDIO_T audioContext = (EMSCRIPTEN_WEBAUDIO_T)userData;
	if (emscripten_audio_context_state(audioContext) != AUDIO_CONTEXT_STATE_RUNNING) {
		emscripten_resume_audio_context_sync(audioContext);
	}

	return EM_FALSE;
}

void AudioWorkletProcessorCreated(EMSCRIPTEN_WEBAUDIO_T audioContext, EM_BOOL success, void *userData) {
	if (!success) return; // Check browser console in a debug build for detailed errors

	int outputChannelCounts[1] = { 1 };
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
		&GenerateNoise,
		0
	);

	// Connect it to audio context destination
	EM_ASM({emscriptenGetAudioObject($0).connect(emscriptenGetAudioObject($1).destination)}, wasmAudioWorklet, audioContext);

	// Resume context on mouse click
	emscripten_set_click_callback("canvas", (void*)audioContext, 0, OnCanvasClick);
}

void AudioThreadInitialized(EMSCRIPTEN_WEBAUDIO_T audioContext, EM_BOOL success, void* userData) {
	if (!success) return; // Check browser console in a debug build for detailed errors

	WebAudioWorkletProcessorCreateOptions opts = {
		.name = "framework-generator",
	};

	emscripten_create_wasm_audio_worklet_processor_async(audioContext, &opts, &AudioWorkletProcessorCreated, 0);
}

EM_BOOL GenerateNoise(int numInputs, const AudioSampleFrame *inputs,
                      int numOutputs, AudioSampleFrame *outputs,
                      int numParams, const AudioParamFrame *params,
                      void *userData)
{
	fw::audio::WebAudioManager* manager = reinterpret_cast<fw::audio::WebAudioManager*>(userData);
	auto processor = manager->getProcessor();

	if (processor) {
		processor->onRender((f32*)pOutput, (const f32*)pInput, frameCount);
	}

	for(int i = 0; i < numOutputs; ++i) {
		for(int j = 0; j < 128*outputs[i].numberOfChannels; ++j) {
			outputs[i].data[j] = emscripten_random() * 0.2 - 0.1; // Warning: scale down audio volume by factor of 0.2, raw noise can be really loud otherwise
		}
	}

	return EM_TRUE; // Keep the graph output going
}

namespace fw::audio {
	WebAudioManager::WebAudioManager() {

	}

	WebAudioManager::~WebAudioManager() {
		stop();
	}

	bool WebAudioManager::setAudioDevice(uint32 idx) {
		return true;
	}

	void WebAudioManager::getDeviceNames(std::vector<std::string>& names) {
	}

	bool WebAudioManager::loadFile(std::string_view path, std::vector<f32>& target) {
		return false;
	}

	bool WebAudioManager::start() {
		EMSCRIPTEN_WEBAUDIO_T context = emscripten_create_audio_context(0);

		emscripten_start_wasm_audio_worklet_thread_async(context, audioThreadStack, sizeof(audioThreadStack),
														 &AudioThreadInitialized, 0);

		return true;
	}

	void WebAudioManager::stop() {

	}

	f32 WebAudioManager::getSampleRate() {
		return 48000;
	}
}

#endif
