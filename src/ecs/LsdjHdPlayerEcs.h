#pragma once

#include <vector>

#include <entt/entity/handle.hpp>
#include <spdlog/spdlog.h>

#include "ui/View.h"
#include "lsdj/LsdjUi.h"
#include "lsdj/LsdjCanvasView.h"
#include "foundation/HashUtil.h"
#include "foundation/StringUtil.h"
#include "ecs/RootContainer.h"
#include "ecs/LsdjController.h"
#include "ecs/RetroPlugProject.h"

namespace rp {
	class LsdjHdPlayerEcs final : public RootContainer {
		FwRegisterObject();
	private:
		LsdjCanvasViewPtr _canvasView;
		RetroPlugProject& _project;
		LsdjController _lsdj;

		entt::entity _system = entt::null;
		lsdj::Ui _ui;

	public:
		LsdjHdPlayerEcs(RetroPlugProject& project, entt::entity system);
		~LsdjHdPlayerEcs();

		void setSystem(entt::entity system);

		void onInitialize() override;

		bool onKey(const fw::KeyEvent& ev) override;

		void onUpdate(f32 delta) override {}

		void onRender(fw::Canvas& canvas) override;
	};
}
