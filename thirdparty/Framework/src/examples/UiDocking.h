#pragma once

#include "audio/AudioManager.h"
#include "ui/ViewManager.h"

#include "ui/WaveView.h"
#include "ui/VerticalSplitter.h"
#include "ui/Dock.h"
//#include "node/AudioGraph.h"
//#include "core/RetroPlugNodes.h"

namespace fw {
	class UiDocking final : public View {
	private:
		bool _ready = false;

		WaveViewPtr _waveView;

		//AudioManager _audioManager;
		//AudioGraph _audioGraph;

		DockSplitterPtr _splitter;
		DockPtr _dock;

		//std::shared_ptr<AudioGraphProcessor> _audioProcessor;

		//std::shared_ptr<SineNode> _sineNode;
		//std::shared_ptr<OutputNode> _outputNode;

	public:
		UiDocking();
		~UiDocking() {}

		void onResize(Dimension dimensions) override;

		//void onDrop(int count, const char** paths) override;

		void onInitialize() override;

		bool onKey(VirtualKey::Enum key, bool down) override;

	private:
		void generateWaveform();
	};
}
