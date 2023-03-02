#include "WebAudioManager.h"

#ifdef FW_PLATFORM_WEB

#include <emscripten/em_math.h>
#include <emscripten/webaudio.h>

#include <spdlog/spdlog.h>

uint8_t audioThreadStack[4096];

EM_BOOL onCanvasClick(int eventType, const EmscriptenMouseEvent *mouseEvent, void *userData) {
	EMSCRIPTEN_WEBAUDIO_T audioContext = (EMSCRIPTEN_WEBAUDIO_T)userData;
	if (emscripten_audio_context_state(audioContext) != AUDIO_CONTEXT_STATE_RUNNING) {
		emscripten_resume_audio_context_sync(audioContext);
	}

	return EM_FALSE;
}

EM_BOOL generateAudio(int numInputs, const AudioSampleFrame *inputs,
                      int numOutputs, AudioSampleFrame *outputs,
                      int numParams, const AudioParamFrame *params,
                      void *userData)
{
	assert(userData);
	fw::audio::WebAudioManager* manager = reinterpret_cast<fw::audio::WebAudioManager*>(userData);
	fw::StereoAudioBuffer& input = manager->getInput();
	fw::StereoAudioBuffer& output = manager->getOutput();
	fw::AudioProcessorPtr processor = manager->getProcessor();
	assert(processor);

	/*for(int i = 0; i < numOutputs; ++i) {
		for(int j = 0; j < 128*outputs[i].numberOfChannels; ++j) {
			outputs[i].data[j] = emscripten_random() * 0.2 - 0.1; // Warning: scale down audio volume by factor of 0.2, raw noise can be really loud otherwise
		}
	}*/

	if (processor) {
		input.clear();

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
	EM_ASM({emscriptenGetAudioObject($0).connect(emscriptenGetAudioObject($1).destination)}, wasmAudioWorklet, audioContext);

	// Resume context on mouse click
	emscripten_set_click_callback("canvas", (void*)audioContext, 0, onCanvasClick);
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
	WebAudioManager::WebAudioManager() {
		_output.resize(128);
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
														 &audioThreadInitialized, this);

		return true;
	}

	void WebAudioManager::stop() {

	}

	f32 WebAudioManager::getSampleRate() {
		return 48000;
	}
}

#endif
