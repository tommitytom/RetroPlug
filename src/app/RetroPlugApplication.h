#pragma once

#include "platform/AudioManager.h"
#include "platform/Application.h"
#include "RetroPlug.h"

namespace rp {
	class RetroPlugApplication final : public Application {
	private:
		NVGcontext* _vg = nullptr;

		RetroPlug _retroPlug;

		bool _ready = false;

	public:
		RetroPlugApplication(const char* name, int32 w, int32 h);
		~RetroPlugApplication() {}

		void onInit() override;

		void onFrame(f64 delta) override;

		void onAudio(const f32* input, f32* output, uint32 frameCount) override;

		void onResize(int32 w, int32 h) override;

		void onDrop(int count, const char** paths) override;

		void onKey(int key, int scancode, int action, int mods) override;

		void onMouseMove(double x, double y) override;

		void onMouseButton(int button, int action, int mods) override;

		void onMouseScroll(double x, double y) override;
	};
}
