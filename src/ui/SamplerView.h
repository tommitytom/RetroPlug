#pragma once

#include "core/SystemWrapper.h"
#include "lsdj/KitUtil.h"
#include "lsdj/LsdjUi.h"
#include "ui/LsdjCanvasView.h"
#include "ui/LsdjModel.h"
#include "ui/MenuView.h"
#include "ui/WaveView.h"
#include "audio/SampleLoaderUtil.h"

namespace rp {
	struct SamplerViewState {
		int32 selectedKit = 0;
		int32 selectedSample = 0;
		//SampleSettings settings;
	};

	class SamplerView final : public LsdjCanvasView {
	private:
		SystemWrapperPtr _system;
		SamplerViewState _samplerState;
		fw::WaveViewPtr _waveView;

		std::vector<KitUtil::SampleData> _sampleBuffers;

		lsdj::Ui _ui;

		bool _aHeld = false;
		bool _bHeld = false;

	public:
		SamplerView();
		~SamplerView() {}

		/*SamplerViewState& getState() {
			return _samplerState;
		}*/

		void setSystem(SystemWrapperPtr& system);

		SystemWrapperPtr getSystem() { return _system; }

		void setSampleIndex(KitIndex kitIdx, size_t sampleIdx);

		void onInitialize() override;

		bool onDrop(const std::vector<std::string>& paths) override;

		bool onKey(const fw::KeyEvent& ev) override;

		void onUpdate(f32 delta) override;

		void onRender(Canvas& canvas) override;

	private:
		void buildMenu(fw::Menu& target);

		void loadSampleDialog(KitIndex kitIndex);

		void updateSampleBuffers();

		void updateWaveform();

		void addKitSamples(KitIndex kitIdx, const std::vector<std::string>& paths);
	};
}
