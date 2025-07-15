#pragma once

#include <vector>

#include <entt/entity/handle.hpp>
#include <spdlog/spdlog.h>

#include "ui/View.h"
#include "core/System.h"
#include "lsdj/LsdjUi.h"
#include "lsdj/LsdjCanvasView.h"
#include "lsdj/LsdjModel.h"
#include "lsdj/LsdjService.h"
#include "ui/SystemOverlayManager.h"
#include "foundation/HashUtil.h"
#include "foundation/StringUtil.h"
#include "ui/SystemContainerView.h"

namespace rp {
	class LsdjHdPlayer final : public SystemContainerView {
		FwRegisterObject();
	private:
		LsdjCanvasViewPtr _canvasView;
		SystemPtr _system;
		lsdj::Ui _ui;

	public:
		LsdjHdPlayer();
		~LsdjHdPlayer() {}

		void setSystem(const SystemPtr& system);

		const SystemPtr& getSystem() { return _system; }

		void onInitialize() override;

		void processInput(std::vector<fw::StreamButtonPress>& buttons, std::vector<std::string>& actions) override;

		bool onKey(const fw::KeyEvent& ev) override;

		void onUpdate(f32 delta) override {}

		void onRender(fw::Canvas& canvas) override;
	};
}
