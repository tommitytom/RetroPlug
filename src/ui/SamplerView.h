#pragma once

#include "core/SystemWrapper.h"
#include "lsdj/KitUtil.h"
#include "lsdj/LsdjUi.h"
#include "ui/LsdjCanvasView.h"
#include "ui/LsdjModel.h"
#include "ui/MenuView.h"
#include "ui/WaveView.h"
#include "util/SampleLoaderUtil.h"

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
		WaveViewPtr _waveView;

		std::vector<KitUtil::SampleData> _sampleBuffers;

		lsdj::Ui _ui;

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
	};
}
