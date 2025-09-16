#pragma once

#include "ui/View.h"
#include "core/Constants.h"

#include "RetroPlugProject.h"

namespace rp {
	class RetroPlugEcsView final : public fw::View {
		FwRegisterObject()

	private:
		static constexpr size_t INVALID_SYSTEM_INDEX = std::numeric_limits<size_t>::max();

		RetroPlugProject& _project;
		uint32 _version = 0;

	public:
		RetroPlugEcsView(RetroPlugProject& project);
		~RetroPlugEcsView() = default;

		void onInitialize() override;

		bool onDrop(const std::vector<std::string>& paths) override;

		void onUpdate(f32 deltaTime) override;

		void onRender(fw::Canvas& canvas) override;

		bool onKey(const fw::KeyEvent& event) override;

	private:
		void rebuildUi();

		entt::registry& getRegistry() {
			return _project.getRegistry();
		}
	};
}
