#pragma once

#include <string>

#include <sol/sol.hpp>

#include "foundation/DataBuffer.h"
#include "audio/AudioManager.h"
#include "core/ProjectState.h"
#include "core/RetroPlugProcessor.h"
#include "core/System.h"
#include "ui/CompactLayoutView.h"
#include "ui/LabelView.h"
#include "ui/TreeView.h"
#include "ui/View.h"
#include "ui/ObjectInspectorView.h"
#include "foundation/ResourceReloader.h"

namespace rp {
	enum class ThreadTarget {
		Ui,
		Audio
	};

	class Project;
	class FileManager;

	class RetroPlugView final : public fw::View {
		RegisterObject();
	private:
		using hrc = std::chrono::high_resolution_clock;

		f64 _nextFrame = 0;

		fw::Uint8Buffer _romBuffer;
		fw::Uint8Buffer _savBuffer;

		std::string _romPath;
		std::string _savPath;

		bool _ready = false;

		IoMessageBus& _ioMessageBus;
		const fw::TypeRegistry& _typeRegistry;

		//std::shared_ptr<RetroPlugProcessor> _audioProcessor;

		CompactLayoutViewPtr _compactLayout;

		Project _project;
		FileManager* _fileManager = nullptr;

		//SystemIndex _selected = INVALID_SYSTEM_IDX;

		uint32 _sampleRate = 48000;

		ThreadTarget _defaultTarget = ThreadTarget::Audio;

		//std::vector<SystemIoPtr> _ioCollection;
		size_t _totalIoAllocated = 0;

		GlobalConfig _config;

		f32 _stateFetchInterval = 1.0f / 60.0f;
		f32 _nextStateFetch;

		bool _doPing = true;
		std::optional<hrc::time_point> _lastPingTime;
		std::optional<hrc::time_point> _lastPongTime;
		bool _audioThreadActive = false;
		fw::LabelViewPtr _threadWarning;

		fw::ResourceReloader _resourceReloader;

		fw::ObjectInspectorViewPtr _inspector;
		fw::TreeViewPtr _viewTree;
		fw::ViewPtr _editContainer;

	public:
		RetroPlugView(const fw::TypeRegistry& typeRegistry, const SystemFactory& systemFactory, IoMessageBus& messageBus);
		~RetroPlugView() = default;

		void onInitialize() override;

		void initViews();

		void onUpdate(f32 delta) override;

		void onRender(fw::Canvas& canvas) override;

		bool onKey(const fw::KeyEvent& ev) override;

		void onHotReload() override;

	private:
		void processOutput();

		void setupEventHandlers();
	};
}
