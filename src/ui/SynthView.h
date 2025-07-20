#pragma once

#include "core/System.h"
#include "lsdj/KitUtil.h"
#include "lsdj/LsdjCanvasView.h"
#include "lsdj/LsdjUi.h"
#include "ui/GridItem.h"
#include "ui/WaveView.h"

namespace rp {
	struct SynthViewState {
		int32 selectedSynth = 0;
		SampleSettings settings;
	};

	class SynthView final : public GridItem {
		FwRegisterObject();
	private:
		SystemPtr _system;
		SynthViewState _samplerState;
		fw::WaveViewPtr _waveView;

		LsdjCanvasViewPtr _canvasView;
		lsdj::Ui _ui;

		uint64 _lastSramHash = 0;

	public:
		SynthView();
		~SynthView() {}

		void setSystem(SystemPtr& system, SystemServicePtr& service);

		SystemPtr getSystem() { return _system; }

		void onInitialize() override;

		bool onDrop(const std::vector<std::string>& paths) override;

		bool onKey(const fw::KeyEvent& ev) override;

		void onUpdate(f32 delta) override;

		void onRender(fw::Canvas& canvas) override;

		void processInput(std::vector<fw::StreamButtonPress>& buttons, std::vector<std::string>& actions) override;

		void createMenu(fw::Menu& target) override;

	private:
		void setWaveform(fw::Float32Buffer& samples);

		void updateWaveform(lsdj::Song& song);
	};
}
