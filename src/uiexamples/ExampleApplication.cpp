#include "ExampleApplication.h"

#include <future>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb/stb_truetype.h>

#include <GLFW/glfw3.h>

#include <bx/bx.h>
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <spdlog/spdlog.h>

#include "util/fs.h"
#include "data/font.h"
#include "data/Early-GameBoy.h"
#include "data/Roboto-Regular.h"
#include "data/PlatNomor.h"

#include "ui/WaveView.h"
#include "ui/WaveformUtil.h"
#include "util/AudioLoaderUtil.h"

#include "node/NodeGraph.h"
#include "core/RetroPlugNodes.h"
#include "node/AudioGraph.h"

using namespace rp;

void testNodeGraph() {
	NodeGraph<AudioGraphProcessor> nodeGraph;
	std::shared_ptr<AudioGraphProcessor> processor = nodeGraph.createProcessor();

	AudioBuffer b1(1024);
	b1.clear(1.0f);

	AudioBuffer b2(1024);
	b2.clear(2.0f);

	auto bn1 = nodeGraph.addNode<AudioBufferNode>();
	auto bn2 = nodeGraph.addNode<AudioBufferNode>();
	auto adder = nodeGraph.addNode<AddNode>();

	bn1->setBuffer(std::move(b1));
	bn2->setBuffer(std::move(b2));

	nodeGraph.connectNodes(bn1, 0, adder, 0);
	nodeGraph.connectNodes(bn2, 0, adder, 1);

	processor->onProcess();
	nodeGraph.onProcess();
}

const bgfx::ViewId kClearView = 0;
const bool s_showStats = false;

ExampleApplication::ExampleApplication(const char* name, int32 w, int32 h) : Application(name, w, h) {
	_waveView = _view.addChild<WaveView>("Wave View");
	_waveView->setDimensions({ 800, 600 });

	generateWaveform();

	_audioProcessor = _audioGraph.createProcessor();
	_outputNode = _audioGraph.addNode<OutputNode>();
	_sineNode = _audioGraph.addNode<SineNode>();
	_audioGraph.connectNodes(_sineNode, 0, _outputNode, 0);
}

bool isApproximately(f32 v, f32 target, f32 epsilon) {
	return v >= target - epsilon && v <= target + epsilon;
}

void ExampleApplication::onInit() {
	bgfx::setViewClear(kClearView, BGFX_CLEAR_COLOR);
	bgfx::setViewRect(kClearView, 0, 0, bgfx::BackbufferRatio::Equal);

	_vg = nvgCreate(0, 0);
	bgfx::setViewMode(0, bgfx::ViewMode::Sequential);

	nvgCreateFontMem(_vg, "Early-GameBoy", Early_GameBoy, (int)Early_GameBoy_len, 0);
	nvgCreateFontMem(_vg, "Roboto-Regular", Roboto_Regular, (int)Roboto_Regular_len, 0);
	nvgCreateFontMem(_vg, "PlatNomor", PlatNomor, (int)PlatNomor_len, 0);

	_view.setVg(_vg);

	_audioManager.setCallback([&](f32* output, const f32* input, uint32 frameCount) {
		_audioProcessor->process(output, input, frameCount);
		
		auto outputNode = std::static_pointer_cast<OutputProcessor>(_audioProcessor->getNodes()[0]);
		AudioBuffer buffer = outputNode->getAudioInput(0);
		assert(buffer.size() == frameCount);

		memset(output, 0, frameCount * sizeof(f32) * 2);
		
		for (size_t i = 0; i < frameCount; ++i) {
			output[i * 2] = buffer.getBuffer().get(i) * 0.1f;
			output[i * 2 + 1] = output[i];
		}
	});

	_audioManager.start();
}

void ExampleApplication::onFrame(f64 delta) {
	bgfx::touch(kClearView);

	NVGcontext* vg = _vg;
	Dimension<uint32> res = getResolution();
	nvgBeginFrame(_vg, (f32)res.w, (f32)res.h, 1.0f);

	_view.onUpdate((f32)delta);
	_view.onRender();

	nvgEndFrame(_vg);

	/*bgfx::dbgTextClear();
	bgfx::dbgTextImage(bx::max<uint16_t>(uint16_t(res.w / 2 / 8), 20) - 20, bx::max<uint16_t>(uint16_t(res.h/ 2 / 16), 6) - 6, 40, 12, s_logo, 160);
	bgfx::dbgTextPrintf(0, 0, 0x0f, "Press F1 to toggle stats.");
	bgfx::dbgTextPrintf(0, 1, 0x0f, "Color can be changed with ANSI \x1b[9;me\x1b[10;ms\x1b[11;mc\x1b[12;ma\x1b[13;mp\x1b[14;me\x1b[0m code too.");
	bgfx::dbgTextPrintf(80, 1, 0x0f, "\x1b[;0m    \x1b[;1m    \x1b[; 2m    \x1b[; 3m    \x1b[; 4m    \x1b[; 5m    \x1b[; 6m    \x1b[; 7m    \x1b[0m");
	bgfx::dbgTextPrintf(80, 2, 0x0f, "\x1b[;8m    \x1b[;9m    \x1b[;10m    \x1b[;11m    \x1b[;12m    \x1b[;13m    \x1b[;14m    \x1b[;15m    \x1b[0m");
	const bgfx::Stats* stats = bgfx::getStats();
	bgfx::dbgTextPrintf(0, 2, 0x0f, "Backbuffer %dW x %dH in pixels, debug text %dW x %dH in characters.", stats->width, stats->height, stats->textWidth, stats->textHeight);
	// Enable stats or debug text.
	bgfx::setDebug(s_showStats ? BGFX_DEBUG_STATS : BGFX_DEBUG_TEXT);*/

	// Advance to next frame. Process submitted rendering primitives.
	bgfx::frame();

	setResolution(_view.getDimensions());
}

void ExampleApplication::onResize(int32 w, int32 h) {
	bgfx::reset((uint32_t)w, (uint32_t)h, BGFX_RESET_VSYNC);
	bgfx::setViewRect(kClearView, 0, 0, bgfx::BackbufferRatio::Equal);
}

void ExampleApplication::onDrop(int count, const char** paths) {
	
}

VirtualKey::Enum convertKey(int key) {
	switch (key) {
	case GLFW_KEY_SPACE: return VirtualKey::Space;
		//case GLFW_KEY_APOSTROPHE: return VirtualKey::Apo;
		//case GLFW_KEY_COMMA: return VirtualKey::comma;
	case GLFW_KEY_MINUS: return VirtualKey::LeftCtrl;
	case GLFW_KEY_PERIOD: return VirtualKey::LeftCtrl;
	case GLFW_KEY_SLASH: return VirtualKey::LeftCtrl;
	case GLFW_KEY_0: return VirtualKey::Num0;
	case GLFW_KEY_1: return VirtualKey::Num1;
	case GLFW_KEY_2: return VirtualKey::Num2;
	case GLFW_KEY_3: return VirtualKey::Num3;
	case GLFW_KEY_4: return VirtualKey::Num4;
	case GLFW_KEY_5: return VirtualKey::Num5;
	case GLFW_KEY_6: return VirtualKey::Num6;
	case GLFW_KEY_7: return VirtualKey::Num7;
	case GLFW_KEY_8: return VirtualKey::Num8;
	case GLFW_KEY_9: return VirtualKey::Num9;
		//case GLFW_KEY_SEMICOLON: return VirtualKey::semi;
		//case GLFW_KEY_EQUAL: return VirtualKey::equa;
	case GLFW_KEY_A: return VirtualKey::A;
	case GLFW_KEY_B: return VirtualKey::B;
	case GLFW_KEY_C: return VirtualKey::C;
	case GLFW_KEY_D: return VirtualKey::D;
	case GLFW_KEY_E: return VirtualKey::E;
	case GLFW_KEY_F: return VirtualKey::F;
	case GLFW_KEY_G: return VirtualKey::G;
	case GLFW_KEY_H: return VirtualKey::H;
	case GLFW_KEY_I: return VirtualKey::I;
	case GLFW_KEY_J: return VirtualKey::J;
	case GLFW_KEY_K: return VirtualKey::K;
	case GLFW_KEY_L: return VirtualKey::L;
	case GLFW_KEY_M: return VirtualKey::M;
	case GLFW_KEY_N: return VirtualKey::N;
	case GLFW_KEY_O: return VirtualKey::O;
	case GLFW_KEY_P: return VirtualKey::P;
	case GLFW_KEY_Q: return VirtualKey::Q;
	case GLFW_KEY_R: return VirtualKey::R;
	case GLFW_KEY_S: return VirtualKey::S;
	case GLFW_KEY_T: return VirtualKey::T;
	case GLFW_KEY_U: return VirtualKey::U;
	case GLFW_KEY_V: return VirtualKey::V;
	case GLFW_KEY_W: return VirtualKey::W;
	case GLFW_KEY_X: return VirtualKey::X;
	case GLFW_KEY_Y: return VirtualKey::Y;
	case GLFW_KEY_Z: return VirtualKey::Z;
		//case GLFW_KEY_LEFT_BRACKET: return VirtualKey::leftbra;
		//case GLFW_KEY_BACKSLASH: return VirtualKey::slas;
	case GLFW_KEY_RIGHT_BRACKET: return VirtualKey::LeftCtrl;
	case GLFW_KEY_GRAVE_ACCENT: return VirtualKey::LeftCtrl;
	case GLFW_KEY_WORLD_1: return VirtualKey::LeftCtrl;
	case GLFW_KEY_WORLD_2: return VirtualKey::LeftCtrl;
	case GLFW_KEY_ESCAPE: return VirtualKey::Esc;
	case GLFW_KEY_ENTER: return VirtualKey::Enter;
	case GLFW_KEY_TAB: return VirtualKey::Tab;
	case GLFW_KEY_BACKSPACE: return VirtualKey::Backspace;
	case GLFW_KEY_INSERT: return VirtualKey::Insert;
	case GLFW_KEY_DELETE: return VirtualKey::Delete;
	case GLFW_KEY_RIGHT: return VirtualKey::RightArrow;
	case GLFW_KEY_LEFT: return VirtualKey::LeftArrow;
	case GLFW_KEY_DOWN: return VirtualKey::DownArrow;
	case GLFW_KEY_UP: return VirtualKey::UpArrow;
	case GLFW_KEY_PAGE_UP: return VirtualKey::PageUp;
	case GLFW_KEY_PAGE_DOWN: return VirtualKey::PageDown;
	case GLFW_KEY_HOME: return VirtualKey::Home;
	case GLFW_KEY_END: return VirtualKey::End;
	case GLFW_KEY_CAPS_LOCK: return VirtualKey::Caps;
	case GLFW_KEY_SCROLL_LOCK: return VirtualKey::Scroll;
	case GLFW_KEY_NUM_LOCK: return VirtualKey::NumLock;
	case GLFW_KEY_PRINT_SCREEN: return VirtualKey::PrintScreen;
	case GLFW_KEY_PAUSE: return VirtualKey::Pause;
	case GLFW_KEY_F1: return VirtualKey::F1;
	case GLFW_KEY_F2: return VirtualKey::F2;
	case GLFW_KEY_F3: return VirtualKey::F3;
	case GLFW_KEY_F4: return VirtualKey::F4;
	case GLFW_KEY_F5: return VirtualKey::F5;
	case GLFW_KEY_F6: return VirtualKey::F6;
	case GLFW_KEY_F7: return VirtualKey::F7;
	case GLFW_KEY_F8: return VirtualKey::F8;
	case GLFW_KEY_F9: return VirtualKey::F9;
	case GLFW_KEY_F10: return VirtualKey::F10;
	case GLFW_KEY_F11: return VirtualKey::F11;
	case GLFW_KEY_F12: return VirtualKey::F12;
	case GLFW_KEY_F13: return VirtualKey::F13;
	case GLFW_KEY_F14: return VirtualKey::F14;
	case GLFW_KEY_F15: return VirtualKey::F15;
	case GLFW_KEY_F16: return VirtualKey::F16;
	case GLFW_KEY_F17: return VirtualKey::F17;
	case GLFW_KEY_F18: return VirtualKey::F18;
	case GLFW_KEY_F19: return VirtualKey::F19;
	case GLFW_KEY_F20: return VirtualKey::F20;
	case GLFW_KEY_F21: return VirtualKey::F21;
	case GLFW_KEY_F22: return VirtualKey::F22;
	case GLFW_KEY_F23: return VirtualKey::F23;
	case GLFW_KEY_F24: return VirtualKey::F24;
		//case GLFW_KEY_F25: return VirtualKey::F25;
	case GLFW_KEY_KP_0: return VirtualKey::NumPad0;
	case GLFW_KEY_KP_1: return VirtualKey::NumPad1;
	case GLFW_KEY_KP_2: return VirtualKey::NumPad2;
	case GLFW_KEY_KP_3: return VirtualKey::NumPad3;
	case GLFW_KEY_KP_4: return VirtualKey::NumPad4;
	case GLFW_KEY_KP_5: return VirtualKey::NumPad5;
	case GLFW_KEY_KP_6: return VirtualKey::NumPad6;
	case GLFW_KEY_KP_7: return VirtualKey::NumPad7;
	case GLFW_KEY_KP_8: return VirtualKey::NumPad8;
	case GLFW_KEY_KP_9: return VirtualKey::NumPad9;
	case GLFW_KEY_KP_DECIMAL: return VirtualKey::Decimal;
	case GLFW_KEY_KP_DIVIDE: return VirtualKey::Divide;
	case GLFW_KEY_KP_MULTIPLY: return VirtualKey::Multiply;
	case GLFW_KEY_KP_SUBTRACT: return VirtualKey::Subtract;
	case GLFW_KEY_KP_ADD: return VirtualKey::Add;
	case GLFW_KEY_KP_ENTER: return VirtualKey::Enter;
		//case GLFW_KEY_KP_EQUAL: return VirtualKey::equal;
	case GLFW_KEY_LEFT_SHIFT: return VirtualKey::LeftShift;
	case GLFW_KEY_LEFT_CONTROL: return VirtualKey::LeftCtrl;
	case GLFW_KEY_LEFT_ALT: return VirtualKey::Alt;
	case GLFW_KEY_LEFT_SUPER: return VirtualKey::LeftWin;
	case GLFW_KEY_RIGHT_SHIFT: return VirtualKey::RightShift;
	case GLFW_KEY_RIGHT_CONTROL: return VirtualKey::RightCtrl;
	case GLFW_KEY_RIGHT_ALT: return VirtualKey::Alt;
	case GLFW_KEY_RIGHT_SUPER: return VirtualKey::RightWin;
		//case GLFW_KEY_MENU: return VirtualKey::LeftMenu;
	}

	return VirtualKey::Unknown;
}

MouseButton::Enum convertMouseButton(int button) {
	switch (button) {
	case GLFW_MOUSE_BUTTON_LEFT: return MouseButton::Left;
	}

	return MouseButton::Unknown;
}

void ExampleApplication::onKey(int key, int scancode, int action, int mods) {
	if (convertKey(key) == VirtualKey::Space) {
		generateWaveform();
	}

	_view.onKey(convertKey(key), action > 0);
}

void ExampleApplication::onMouseMove(double x, double y) {
	_view.onMouseMove(Point<uint32>((uint32)x, (uint32)y));
}

void ExampleApplication::onMouseButton(int button, int action, int mods) {
	_view.onMouseButton(convertMouseButton(button), action > 0);
}

void ExampleApplication::onMouseScroll(double x, double y) {
	_view.onMouseScroll(Point<f32>((f32)x, (f32)y));
}

void ExampleApplication::generateWaveform() {
	Float32Buffer samples;
	AudioLoaderUtil::load("c:\\temp\\telewizor.wav", samples);
	size_t sampleCount = samples.size();

	_waveView->setAudioData(std::move(samples));

	size_t offset = 0;
	size_t markerStep = 44100;
	while (offset < sampleCount) {
		_waveView->addMarker((f32)offset);
		offset += markerStep;
	}
}