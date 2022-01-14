#pragma once

#include <unordered_map>

#include <nanovg.h>
#include <sol/sol.hpp>

#include "RpMath.h"
#include "core/Project.h"
#include "core/System.h"
#include "core/SystemManager.h"
#include "core/SystemProcessor.h"
#include "core/SystemSettings.h"
#include "core/SystemOrchestrator.h"
#include "core/Proxies.h"
#include "core/UiState.h"
#include "ui/ViewManager.h"

namespace rp {
	enum class ThreadTarget {
		Ui,
		Audio
	};

	const size_t MAX_IO_STREAMS = 16;
	class Project;

	class UiContext {
	private:
		UiState _state;
		SystemOrchestrator _orchestrator;
		Project* _project;

		NVGcontext* _vg = nullptr;

		//SystemIndex _selected = INVALID_SYSTEM_IDX;

		uint32 _sampleRate = 48000;

		ThreadTarget _defaultTarget = ThreadTarget::Audio;

		IoMessageBus* _ioMessageBus;
		OrchestratorMessageBus* _orchestratorMessageBus;

		//std::vector<SystemIoPtr> _ioCollection;
		size_t _totalIoAllocated = 0;

	public:
		UiContext(IoMessageBus* messageBus, OrchestratorMessageBus* orchestratorMessageBus);
		~UiContext() {}

		Dimension<uint32> getDimensions() {
			Project* proj = _state.viewManager.getShared<Project>();
			uint32 zoom = proj->getState().settings.zoom + 1;
			auto dimensions = _state.viewManager.getDimensions();

			return Dimension<uint32> { dimensions.w * zoom, dimensions.h * zoom };
		}

		UiState& getState() {
			return _state;
		}

		void setSampleRate(uint32 sampleRate) {
			_sampleRate = sampleRate;
		}

		void setNvgContext(NVGcontext* vg);

		void processDelta(f64 delta);

		bool onKey(VirtualKey::Enum key, bool down);

		void onDrop(int count, const char** paths);

		void onMouseMove(double x, double y);

		void onMouseButton(MouseButton::Enum button, bool down);

		void onMouseScroll(double x, double y);

	private:
		void processInput(uint32 frameCount);

		void processOutput();
	};
}