#pragma once

#include <nanovg.h>

#include "platform/Application.h"
#include "platform/AudioManager.h"
#include "ui/ViewManager.h"

#include "ui/WaveView.h"
#include "ui/VerticalSpliiter.h"
#include "node/AudioGraph.h"
#include "core/RetroPlugNodes.h"

namespace rp {
	class ExampleApplication final : public Application {
	private:
		NVGcontext* _vg = nullptr;
		bool _ready = false;

		ViewManager _view;
		WaveViewPtr _waveView;

		AudioManager _audioManager;
		AudioGraph _audioGraph;

		DockSplitterPtr _splitter;
		DockPtr _dock;

		std::shared_ptr<AudioGraphProcessor> _audioProcessor;

		std::shared_ptr<SineNode> _sineNode;
		std::shared_ptr<OutputNode> _outputNode;

	public:
		ExampleApplication(const char* name, int32 w, int32 h);
		~ExampleApplication() {}

		void onInit() override;

		void onFrame(f64 delta) override;

		void onResize(int32 w, int32 h) override;

		void onDrop(int count, const char** paths) override;

		void onKey(int key, int scancode, int action, int mods) override;

		void onMouseMove(double x, double y) override;

		void onMouseButton(int button, int action, int mods) override;

		void onMouseScroll(double x, double y) override;

	private:
		void generateWaveform();
	};
}
