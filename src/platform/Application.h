#pragma once

#include <string>

#include "platform/Types.h"
#include "platform/AudioManager.h"
#include "RpMath.h"

namespace rp {
	class Application {
	private:
		std::string _name;
		Dimension<uint32> _resolution;

		f64 _time = 0.0;
		bool _closeRequested = false;
		bool _hasResized = false;

		AudioManager _audioManager;

	public:
		Application(const char* name, int32 w, int32 h): _name(name), _resolution(w, h) {
			_audioManager.setCallback([&](f32* output, const f32* input, uint32 frameCount) {
				onAudio(input, output, frameCount);
			});
		}
		//virtual ~Application() = 0;

		virtual void onInit() = 0;

		virtual void onFrame(f64 delta) = 0;

		virtual void onAudio(const f32* input, f32* output, uint32 frameCount) {}

		virtual void onResize(int32 w, int32 h) = 0;

		virtual void onDrop(int count, const char** paths) = 0;

		virtual void onKey(int key, int scancode, int action, int mods) = 0;

		virtual void onMouseMove(double x, double y) = 0;

		virtual void onMouseButton(int button, int action, int mods) = 0;

		virtual void onMouseScroll(double x, double y) = 0;

		void startAudio() { _audioManager.start(); }

		void stopAudio() { _audioManager.stop(); }

		void setResolution(Dimension<uint32> res) {
			if (_resolution != res) {
				_resolution = res;
				_hasResized = true;
			}
		}

		bool hasResized() {
			bool resized = _hasResized;
			_hasResized = false;
			return resized;
		}

		Dimension<uint32> getResolution() const {
			return _resolution;
		}

		const std::string& getName() const {
			return _name;
		}

	private:
		void handleFrame(f64 time) {
			f64 delta = _time > 0 ? time - _time : 0;
			_time = time;
			onFrame(delta);
		}

		void handleResize(int32 w, int32 h);

		void handleKey(int key, int scancode, int action, int mods);

		void handleDrop(int count, const char** paths);

		void handleMouseMove(double x, double y);

		void handleMouseButton(int button, int action, int mods);

		void handleMouseScroll(double x, double y);

		friend class Window;
	};
}
