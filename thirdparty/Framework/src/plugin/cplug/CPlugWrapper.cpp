#include <pugl/pugl.h>
#include <pugl/gl.h>
#include <cplug.h>
#include <vst3_c_api.h>

#include "audio/AudioBuffer.h"
#include "entry/ApplicationFactory.h"
#include "application/Application.h"
#include "graphics/gl/GlRenderContext.h"

struct CPlugPlugin {
	std::unique_ptr<fw::app::Application> app;
	fw::audio::AudioManagerPtr audioManager;

	fw::StereoAudioBuffer input;
	fw::StereoAudioBuffer output;

	uint32 _maxBlockSize = 0;
	bool transportRunning = false;
};

struct CPlugGui {
	std::unique_ptr<fw::app::UiContext> uiContext;
	CPlugPlugin* plugin;

	PuglWorld* puglWorld;
	PuglView* puglView;

	fw::ViewPtr appView;

	std::optional<fw::EventNode> eventNode;
};

static fw::MouseButton convertMouseButton(uint32 button) {
	switch (button) {
	case 0: return fw::MouseButton::Left;
	case 1: return fw::MouseButton::Right;
	case 2: return fw::MouseButton::Middle;
	default: return fw::MouseButton::Unknown;
	}
}

static int AsciiToVK(int ascii) {
#ifdef FW_OS_WINDOWS
	HKL layout = GetKeyboardLayout(0);
	return VkKeyScanExA((CHAR)ascii, layout);
#else
	// Numbers and uppercase alpha chars map directly to VK
	if ((ascii >= 0x30 && ascii <= 0x39) || (ascii >= 0x41 && ascii <= 0x5A)) {
		return ascii;
	}

	// Lowercase alpha chars map to VK but need shifting
	if (ascii >= 0x61 && ascii <= 0x7A) {
		return ascii - 0x20;
	}

	return 0;
#endif
}

static int VSTKeyCodeToVK(int code, int ascii) {
	// If the keycode provided by the host is 0, we can still calculate the VK from the ascii value
	if (code == 0) {
		return AsciiToVK(ascii);
	}

	switch (code) {
	case Steinberg_KEY_BACK: return VK_BACK;
	case Steinberg_KEY_TAB: return VK_TAB;
	case Steinberg_KEY_CLEAR: return VK_CLEAR;
	case Steinberg_KEY_RETURN: return VK_RETURN;
	case Steinberg_KEY_PAUSE: return VK_PAUSE;
	case Steinberg_KEY_ESCAPE: return VK_ESCAPE;
	case Steinberg_KEY_SPACE: return VK_SPACE;
	case Steinberg_KEY_NEXT: return VK_NEXT;
	case Steinberg_KEY_END: return VK_END;
	case Steinberg_KEY_HOME: return VK_HOME;
	case Steinberg_KEY_LEFT: return VK_LEFT;
	case Steinberg_KEY_UP: return VK_UP;
	case Steinberg_KEY_RIGHT: return VK_RIGHT;
	case Steinberg_KEY_DOWN: return VK_DOWN;
	case Steinberg_KEY_PAGEUP: return VK_PRIOR;
	case Steinberg_KEY_PAGEDOWN: return VK_NEXT;
	case Steinberg_KEY_SELECT: return VK_SELECT;
	case Steinberg_KEY_PRINT: return VK_PRINT;
	case Steinberg_KEY_ENTER: return VK_RETURN;
	case Steinberg_KEY_SNAPSHOT: return VK_SNAPSHOT;
	case Steinberg_KEY_INSERT: return VK_INSERT;
	case Steinberg_KEY_DELETE: return VK_DELETE;
	case Steinberg_KEY_HELP: return VK_HELP;
	case Steinberg_KEY_NUMPAD0: return VK_NUMPAD0;
	case Steinberg_KEY_NUMPAD1: return VK_NUMPAD1;
	case Steinberg_KEY_NUMPAD2: return VK_NUMPAD2;
	case Steinberg_KEY_NUMPAD3: return VK_NUMPAD3;
	case Steinberg_KEY_NUMPAD4: return VK_NUMPAD4;
	case Steinberg_KEY_NUMPAD5: return VK_NUMPAD5;
	case Steinberg_KEY_NUMPAD6: return VK_NUMPAD6;
	case Steinberg_KEY_NUMPAD7: return VK_NUMPAD7;
	case Steinberg_KEY_NUMPAD8: return VK_NUMPAD8;
	case Steinberg_KEY_NUMPAD9: return VK_NUMPAD9;
	case Steinberg_KEY_MULTIPLY: return VK_MULTIPLY;
	case Steinberg_KEY_ADD: return VK_ADD;
	case Steinberg_KEY_SEPARATOR: return VK_SEPARATOR;
	case Steinberg_KEY_SUBTRACT: return VK_SUBTRACT;
	case Steinberg_KEY_DECIMAL: return VK_DECIMAL;
	case Steinberg_KEY_DIVIDE: return VK_DIVIDE;
	case Steinberg_KEY_F1: return VK_F1;
	case Steinberg_KEY_F2: return VK_F2;
	case Steinberg_KEY_F3: return VK_F3;
	case Steinberg_KEY_F4: return VK_F4;
	case Steinberg_KEY_F5: return VK_F5;
	case Steinberg_KEY_F6: return VK_F6;
	case Steinberg_KEY_F7: return VK_F7;
	case Steinberg_KEY_F8: return VK_F8;
	case Steinberg_KEY_F9: return VK_F9;
	case Steinberg_KEY_F10: return VK_F10;
	case Steinberg_KEY_F11: return VK_F11;
	case Steinberg_KEY_F12: return VK_F12;
	case Steinberg_KEY_NUMLOCK: return VK_NUMLOCK;
	case Steinberg_KEY_SCROLL: return VK_SCROLL;
	case Steinberg_KEY_SHIFT: return VK_SHIFT;
	case Steinberg_KEY_CONTROL: return VK_CONTROL;
	case Steinberg_KEY_ALT: return VK_MENU;
	case Steinberg_KEY_EQUALS: return VK_OEM_PLUS;
	}

	return 0;
}

extern "C" {
	void cplug_libraryLoad() {}
	void cplug_libraryUnload() {}

	void* cplug_createPlugin() {
		freopen("C:\\temp\\vstlog.txt", "a", stderr);

		fw::audio::AudioManagerPtr audioManager = std::make_shared<fw::audio::AudioManager>();
		std::unique_ptr<fw::app::Application> app = fw::ApplicationFactory::create();
		audioManager->setProcessor(app->onCreateAudio());

		CPlugPlugin* plugin = new CPlugPlugin{ 
			.app = std::move(app),
			.audioManager = audioManager,
		};

		return plugin;
	}

	void cplug_destroyPlugin(void* ptr) {
		delete static_cast<CPlugPlugin*>(ptr);
	}

	// Bus information
	uint32_t cplug_getInputBusChannelCount(void* ptr, uint32_t idx) {
		return idx == 0 ? 2 : 0;
	}

	uint32_t cplug_getOutputBusChannelCount(void* ptr, uint32_t idx) {
		return idx == 0 ? 2 : 0;
	}

	const char* cplug_getInputBusName(void* ptr, uint32_t idx) {
		return idx == 0 ? "Stereo Input" : "";
	}

	const char* cplug_getOutputBusName(void* ptr, uint32_t idx) {
		return idx == 0 ? "Stereo Output" : "";
	}

	// Parameter interface
	const char* cplug_getParameterName(void* ptr, uint32_t index) {
		return "Unknown";
	}

	double cplug_getParameterValue(void* ptr, uint32_t index) {
		return 0;
	}

	double cplug_getDefaultParameterValue(void* ptr, uint32_t index) {
		return 0;
	}

	void cplug_setParameterValue(void* ptr, uint32_t index, double value) {

	}

	double cplug_denormaliseParameterValue(void* ptr, uint32_t index, double normalised) {
		return 0;
	}

	double cplug_normaliseParameterValue(void* ptr, uint32_t index, double denormalised) {
		return 0;
	}

	double cplug_parameterStringToValue(void* ptr, uint32_t index, const char* str) {
		return 0;
	}

	void cplug_parameterValueToString(void* ptr, uint32_t index, char* buf, size_t bufsize, double value) {
		snprintf(buf, bufsize, "%.2f", 0.0);
	}

	void cplug_getParameterRange(void* ptr, uint32_t index, double* min, double* max) {
		*min = 0.0;
		*max = 0.0;
	}

	uint32_t cplug_getParameterFlags(void* ptr, uint32_t index) {
		return 0;
	}

	// Audio processing
	uint32_t cplug_getLatencyInSamples(void* ptr) { return 0; }
	uint32_t cplug_getTailInSamples(void* ptr) { return 0; }

	void cplug_setSampleRateAndBlockSize(void* ptr, double sampleRate, uint32_t maxBlockSize) {
		CPlugPlugin* plugin = static_cast<CPlugPlugin*>(ptr);
		plugin->audioManager->setSampleRate((f32)sampleRate);
		//plugin->setSampleRateAndBlockSize(sampleRate, maxBlockSize);
	}

	void cplug_process(void* ptr, CplugProcessContext* ctx) {
		CPlugPlugin* plugin = static_cast<CPlugPlugin*>(ptr);

		auto processor = plugin->audioManager->getProcessor();
		processor->onBeginUpdate(ctx->numFrames);

		const bool transportRunning = ctx->flags & CPLUG_FLAG_TRANSPORT_IS_PLAYING;
		if (transportRunning != plugin->transportRunning) {
			plugin->transportRunning = transportRunning;

			if (processor) {
				processor->onTransportChange(transportRunning);
			}
		}

		processor->onTransportUpdate(fw::TimeInfo{
			.sampleRate = plugin->audioManager->getSampleRate(),
			.tempo = ctx->bpm,
			//.samplePos = ctx->playheadBeats,
			.ppqPos = ctx->playheadBeats,
			//.lastBar = mTimeInfo.mLastBar,
			.cycleStart = ctx->loopStartBeats,
			.cycleEnd = ctx->loopEndBeats,

			.numerator = static_cast<int>(ctx->timeSigNumerator),
			.denominator = static_cast<int>(ctx->timeSigDenominator),
			.frameCount = static_cast<uint32>(ctx->numFrames),

			.transportIsRunning = transportRunning,
			.transportLoopEnabled = (ctx->flags & CPLUG_FLAG_TRANSPORT_IS_LOOPING) != 0
		});

		plugin->input.resize((uint32)ctx->numFrames);
		plugin->output.resize((uint32)ctx->numFrames);

		/*
		if (inputs) {
			for (uint32 i = 0; i < plugin->input.getFrameCount(); ++i) {
				for (uint32 j = 0; j < plugin->input.ChannelCount; ++j) {
					input.setSample(i, j, inputs[j][i]);
				}
			}
		}
		*/

		CplugEvent event;
		int frame = 0;
		while (ctx->dequeueEvent(ctx, &event, frame)) {
			switch (event.type) {
			case CPLUG_EVENT_PARAM_CHANGE_UPDATE:
			{
				break;
			}
			case CPLUG_EVENT_MIDI:
			{
				plugin->audioManager->getProcessor()->onMidi(fw::MidiMessage{
					.status = event.midi.status,
					.data1 = event.midi.data1,
					.data2 = event.midi.data2
				});
				break;
			}
			case CPLUG_EVENT_PROCESS_AUDIO:
			{
				float** output = ctx->getAudioOutput(ctx, 0);
				CPLUG_LOG_ASSERT(output != NULL)
				CPLUG_LOG_ASSERT(output[0] != NULL);
				CPLUG_LOG_ASSERT(output[1] != NULL);

				//auto in = plugin->input.slice(frame, event.processAudio.endFrame - frame);
				//auto out = plugin->output.slice(frame, event.processAudio.endFrame - frame);

				const uint32 frameCount = event.processAudio.endFrame - frame;
				plugin->audioManager->process(plugin->output.getSamples(), plugin->input.getSamples(), frameCount);

				for (uint32 j = 0; j < plugin->output.ChannelCount; ++j) {
					for (uint32 i = 0; i < frameCount; ++i) {
						output[j][i] = plugin->output.getSample(i, j);
					}
				}

				frame = event.processAudio.endFrame;

				break;
			}
			default:
				break;
			}
		}
	}

	// State management
	void cplug_saveState(void* userPlugin, const void* stateCtx, cplug_writeProc writeProc) {
		CPlugPlugin* plugin = static_cast<CPlugPlugin*>(userPlugin);
		fw::Uint8Buffer buffer;
		plugin->audioManager->getProcessor()->onSerialize(buffer);
		writeProc(stateCtx, buffer.data(), buffer.size());
	}

	void cplug_loadState(void* userPlugin, const void* stateCtx, cplug_readProc readProc) {
		CPlugPlugin* plugin = static_cast<CPlugPlugin*>(userPlugin);
		fw::Uint8Buffer buffer(1024 * 1024);
		int64_t readSize = readProc(stateCtx, buffer.data(), buffer.size());
		buffer.resize((size_t)readSize);
		plugin->audioManager->getProcessor()->onDeserialize(buffer);
	}

	static PuglStatus onPuglEvent(PuglView* view, const PuglEvent* event) {
		CPlugGui* gui = static_cast<CPlugGui*>(puglGetHandle(view));
		fw::app::WindowPtr window = gui->uiContext ? gui->uiContext->getMainWindow() : nullptr;
		fw::ViewManagerPtr viewManager = window ? window->getViewManager() : nullptr;

		switch (event->type) {
		case PUGL_REALIZE: {
			fw::ResourceManagerPtr resourceManager = std::make_shared<fw::ResourceManager>();
			std::unique_ptr<fw::app::UiContext> uiContext = std::make_unique<fw::app::UiContext>(
				std::make_unique<fw::GlRenderContext>(false),
				std::make_unique<fw::app::WrappedWindowManager>(
					resourceManager,
					std::make_shared<fw::FontManager>(resourceManager)
				)
			);

			window = uiContext->setupNativeWindow(gui->appView, nullptr, gui->appView->getDimensions());
			
			viewManager = window->getViewManager();
			viewManager->createState(gui->plugin->audioManager.get());
			viewManager->createState<fw::EventNode>(std::move(gui->eventNode.value()));

			gui->uiContext = std::move(uiContext);
			
			break;
		}
		case PUGL_UNREALIZE:
			gui->uiContext = nullptr;
			break;
		case PUGL_CONFIGURE:
			window->setDimensions(fw::Dimension{ event->configure.width, event->configure.height });
			break;
		case PUGL_EXPOSE:
			gui->uiContext->runFrame();
			break;

		case PUGL_UPDATE:
			puglObscureView(view);
			break;

		case PUGL_LOOP_ENTER:
			// Start timer when PUGL enters its event loop
			//puglStartTimer(view, updateTimerId, 1.0 / 60.0); // 60 FPS
			break;
		case PUGL_LOOP_LEAVE:
			//puglStopTimer(view, updateTimerId);
			break;
		case PUGL_TIMER:
			puglUpdate(gui->puglWorld, 0.0);
			//puglObscureView(view);
			break;

		case PUGL_MOTION:
			viewManager->onMouseMove(fw::Point{ (int32)event->motion.x, (int32)event->motion.y });
			break;

		case PUGL_BUTTON_PRESS:
		case PUGL_BUTTON_RELEASE: 
			viewManager->onMouseButton(fw::MouseButtonEvent{
				.button = convertMouseButton(event->button.button),
				.down = event->type == PUGL_BUTTON_PRESS,
				.position = fw::Point{ (int32)event->button.x, (int32)event->button.y }
			});
			break;

		case PUGL_KEY_PRESS:
		case PUGL_KEY_RELEASE:
			viewManager->onKey(fw::KeyEvent{
				.action = event->type == PUGL_KEY_PRESS ? fw::KeyAction::Press : fw::KeyAction::Release,
				.key = static_cast<fw::VirtualKey>(event->key.keycode),
				.down = event->type == PUGL_KEY_PRESS,
			});
			break;

		case PUGL_TEXT:
			spdlog::info("Text input: {}", event->text.character);
			break;

		default:
			break;
		}

		return PUGL_SUCCESS;
	}

	void* cplug_createGUI(void* userPlugin) {
		CPlugPlugin* plugin = static_cast<CPlugPlugin*>(userPlugin);

		CPlugGui* gui = new CPlugGui{
			.plugin = plugin,
			.appView = plugin->app->onCreateUi(),
		};

		fw::EventNode& audioNode = plugin->audioManager->getProcessor()->getEventNode();
		audioNode.update();
		gui->eventNode = audioNode.spawn("Ui");

		gui->puglWorld = puglNewWorld(PUGL_MODULE, 0);
		gui->puglView = puglNewView(gui->puglWorld);

		puglSetHandle(gui->puglView, gui);
		puglSetEventFunc(gui->puglView, onPuglEvent);
		puglSetBackend(gui->puglView, puglGlBackend());
		
		// Set size hints
		puglSetSizeHint(gui->puglView, PUGL_DEFAULT_SIZE, gui->appView->getDimensions().w, gui->appView->getDimensions().h);
		puglSetSizeHint(gui->puglView, PUGL_MIN_SIZE, 1, 1);

		// Configure GL context
		puglSetViewHint(gui->puglView, PUGL_CONTEXT_API, PUGL_OPENGL_API);
		puglSetViewHint(gui->puglView, PUGL_CONTEXT_VERSION_MAJOR, 3);
		puglSetViewHint(gui->puglView, PUGL_CONTEXT_VERSION_MINOR, 3);
		puglSetViewHint(gui->puglView, PUGL_CONTEXT_PROFILE, PUGL_OPENGL_CORE_PROFILE);
		puglSetViewHint(gui->puglView, PUGL_DOUBLE_BUFFER, PUGL_TRUE);

		return gui;
	}

	void cplug_destroyGUI(void* userGUI) {
		CPlugGui* gui = static_cast<CPlugGui*>(userGUI);
		
		if (gui->puglView) {
			puglFreeView(gui->puglView);
		}
		if (gui->puglWorld) {
			puglFreeWorld(gui->puglWorld);
		}

		delete gui;
	}

	void cplug_setParent(void* userGUI, void* newParent) {
		CPlugGui* gui = static_cast<CPlugGui*>(userGUI);
		if (newParent) {
			// Parent PUGL view to host's window
			puglSetParent(gui->puglView, (uintptr_t)newParent);

			puglRealize(gui->puglView);
			puglShow(gui->puglView, PUGL_SHOW_RAISE);	
			puglStartTimer(gui->puglView, 0, 1.0 / 60.0); // 60 FPS
		} else {
			puglStopTimer(gui->puglView, 0);
			puglHide(gui->puglView);
			puglSetParent(gui->puglView, 0);
		}
	}

	void cplug_setVisible(void* userGUI, bool visible) {
		CPlugGui* gui = static_cast<CPlugGui*>(userGUI);
	}

	void cplug_setScaleFactor(void* userGUI, float scale) {
		CPlugGui* gui = static_cast<CPlugGui*>(userGUI);
		if (gui->uiContext) {
			gui->uiContext->getMainWindow()->getViewManager()->setScale(scale);
		} else if (gui->appView) {
			gui->appView->setScale(scale);
		}
	}

	void cplug_onKeyDown(void* userGUI, const int16_t key_char, const int16_t key_code, const int16_t modifiers) {
		CPlugGui* gui = static_cast<CPlugGui*>(userGUI);
		gui->uiContext->getMainWindow()->getViewManager()->onKey(fw::KeyEvent{
			.action = fw::KeyAction::Press,
			.key = static_cast<fw::VirtualKey>(VSTKeyCodeToVK(key_code, key_char)),
			.down = true,
		});
	}

	void cplug_onKeyUp(void* userGUI, const int16_t key_char, const int16_t key_code, const int16_t modifiers) {
		CPlugGui* gui = static_cast<CPlugGui*>(userGUI);
		gui->uiContext->getMainWindow()->getViewManager()->onKey(fw::KeyEvent{
			.action = fw::KeyAction::Release,
			.key = static_cast<fw::VirtualKey>(VSTKeyCodeToVK(key_code, key_char)),
			.down = false,
		});
	}

	void cplug_getSize(void* userGUI, uint32_t* width, uint32_t* height) {
		CPlugGui* gui = static_cast<CPlugGui*>(userGUI);
		if (gui->appView) {
			fw::Dimension dim = gui->appView->getDimensions();
			*width = dim.w;
			*height = dim.h;
		}
	}

	bool cplug_setSize(void* userGUI, uint32_t width, uint32_t height) {
		CPlugGui* gui = static_cast<CPlugGui*>(userGUI);
		if (gui->uiContext) {
			gui->uiContext->getMainWindow()->setDimensions(fw::Dimension{ (int)width, (int)height });
		}
		return true;
	}

	void cplug_checkSize(void* userGUI, uint32_t* width, uint32_t* height) {
		CPlugGui* gui = static_cast<CPlugGui*>(userGUI);
	}

	bool cplug_getResizeHints(
		void* userGUI,
		bool* resizableX,
		bool* resizableY,
		bool* preserveAspectRatio,
		uint32_t* aspectRatioX,
		uint32_t* aspectRatioY)
	{
		CPlugGui* gui = static_cast<CPlugGui*>(userGUI);
		return true;
	}

} // extern "C"
