#pragma once

#include <vector>

#include <entt/entity/handle.hpp>
#include <spdlog/spdlog.h>

#include "foundation/HashUtil.h"
#include "foundation/StringUtil.h"

#include "core/RetroPlugProject.h"
#include "ui/RootContainer.h"
#include "ui/View.h"

#include "lsdj/LsdjUi.h"
#include "lsdj/LsdjCanvasView.h"
#include "lsdj/LsdjController.h"

namespace rp {
	class LsdjHdPlayer final : public RootContainer {
		FwRegisterObject();
	private:
		LsdjCanvasViewPtr _canvasView;
		RetroPlugProject& _project;
		LsdjController _lsdj;

		entt::entity _system = entt::null;
		lsdj::Ui _ui;

	public:
		LsdjHdPlayer(RetroPlugProject& project, entt::entity system);
		~LsdjHdPlayer();

		void setSystem(entt::entity system);

		void onInitialize() override;

		bool onKey(const orb::KeyEvent& ev) override;

		void onUpdate(f32 delta) override {}

		void onRender(orb::Canvas& canvas) override;
	};
}
