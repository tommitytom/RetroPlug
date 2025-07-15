#pragma once

#include <string>

#include "foundation/DataBuffer.h"
#include "foundation/GainputGamepadManager.h"
#include "foundation/ResourceReloader.h"
#include "audio/AudioManager.h"
#include "core/InputManager.h"
#include "core/ProjectState.h"
#include "core/RetroPlugProcessor.h"
#include "core/System.h"
#include "ui/CompactLayoutView.h"
#include "ui/LabelView.h"
#include "ui/TreeView.h"
#include "ui/View.h"
#include "ui/PanelView.h"
#include "ui/ObjectInspectorView.h"
#include "ui/FileDialogManager.h"

namespace rp {
	enum class ThreadTarget {
		Ui,
		Audio
	};

	class Project;
	class FileManager;

	class RetroPlugView final : public fw::View {
		FwRegisterObject();
	private:
		using hrc = std::chrono::high_resolution_clock;

		IoMessageBus& _ioMessageBus;
		const fw::TypeRegistry& _typeRegistry;
		FileManager _fileManager;
		InputManager _inputManager;
		fw::FileDialogManager _fileDialogManager;

		std::vector<fw::StreamButtonPress> _buttons;
		fw::ButtonStreamWriter _buttonWriter;

		SystemContainerViewPtr _systemContainer;
		//CompactLayoutViewPtr _compactLayout;
		Project _project;

		uint32 _sampleRate = 48000;

		ThreadTarget _defaultTarget = ThreadTarget::Audio;

		//GlobalConfig _config;

		f32 _stateFetchInterval = 1.0f / 60.0f;
		f32 _nextStateFetch = _stateFetchInterval;

		bool _doPing = true;
		std::optional<hrc::time_point> _lastPingTime;
		std::optional<hrc::time_point> _lastPongTime;
		bool _audioThreadActive = false;
		fw::PanelViewPtr _threadWarning;

		fw::ResourceReloader _resourceReloader;

		std::weak_ptr<MenuView> _menu;
		RetroPlugConfig _config;

		fw::GainputGamepadManager _gamepadManager;

	public:
		RetroPlugView(const fw::TypeRegistry& typeRegistry, const SystemFactory& systemFactory, IoMessageBus& messageBus, const RetroPlugConfig& config);
		~RetroPlugView() = default;

		void onInitialize() override;

		void onUpdate(f32 delta) override;

		void onRender(fw::Canvas& canvas) override;

		bool onKey(const fw::KeyEvent& ev) override;

		void onHotReload() override;

		bool onCloseWindowRequest(fw::CloseWindowContext& ctx) override;

	private:
		void initViews(SystemContainerViewPtr container);

		void processOutput();

		void setupEventHandlers();
	};
}
