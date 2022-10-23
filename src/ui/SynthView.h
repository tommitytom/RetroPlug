#pragma once

#include "core/System.h"
#include "ui/LsdjCanvasView.h"
#include "lsdj/LsdjUi.h"
#include "ui/WaveView.h"
#include "lsdj/KitUtil.h"

namespace rp {
	struct SynthViewState {
		int32 selectedSynth = 0;
		SampleSettings settings;
	};

	class SynthView final : public LsdjCanvasView {
	private:
		SystemPtr _system;
		SynthViewState _samplerState;
		fw::WaveViewPtr _waveView;

		lsdj::Ui _ui;

		uint64 _lastSramHash = 0;

	public:
		SynthView();
		~SynthView() {}

		void setSystem(SystemPtr& system);

		bool onDrop(const std::vector<std::string>& paths) override;

		bool onKey(VirtualKey::Enum key, bool down) override;

		void onUpdate(f32 delta) override;

		void onRender(Canvas& canvas) override;

	private:
		void setWaveform(fw::Float32Buffer& samples);

		void updateWaveform(lsdj::Song& song);
	};
}
