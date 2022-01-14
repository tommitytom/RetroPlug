#pragma once

#include "core/System.h"
#include "ui/LsdjCanvasView.h"
#include "lsdj/LsdjUi.h"
#include "ui/WaveView.h"
#include "ui/MenuView.h"
#include "ui/LsdjModel.h"
#include "util/SampleLoaderUtil.h"
#include "lsdj/KitUtil.h"

namespace rp {
	struct SamplerViewState {
		int32 selectedKit = 0;
		int32 selectedSample = 0;
		//SampleSettings settings;
	};

	class SamplerView final : public LsdjCanvasView {
	private:
		SystemPtr _system;
		SamplerViewState _samplerState;
		WaveViewPtr _waveView;

		std::vector<KitUtil::SampleData> _sampleBuffers;

		lsdj::Ui _ui;

		bool _bHeld = false;

	public:
		SamplerView();
		~SamplerView() {}

		void setSystem(SystemPtr& system);

		SystemPtr getSystem() { return _system; }

		void onInitialized() override;

		bool onDrop(const std::vector<std::string>& paths) override;

		bool onKey(VirtualKey::Enum key, bool down) override;

		void onUpdate(f32 delta) override;

		void onRender() override;

	private:
		void buildMenu(Menu& target);

		void loadSampleDialog(KitIndex kitIndex);

		void updateSampleBuffers();

		void updateWaveform();

		void addKitSamples(KitIndex kitIdx, const std::vector<std::string>& paths);

		LsdjModel* getModel();
	};
}
