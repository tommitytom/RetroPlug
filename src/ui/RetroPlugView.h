
#pragma once

#include "ui/View.h"
#include "core/Constants.h"

#include "core/RetroPlugProject.h"
#include "ui/RootContainer.h"
#include "foundation/FileWatcher.h"

namespace rp {
	class RetroPlugView final : public orb::View {
		FwRegisterObject()

	private:
		static constexpr size_t INVALID_SYSTEM_INDEX = std::numeric_limits<size_t>::max();

		std::shared_ptr<RootContainer> _rootContainer;
		RetroPlugProject& _project;
		uint32 _version = 0;
		#ifdef FW_PLATFORM_WEB
		orb::DummyFileWatcher _watcher;
		#else
		orb::EfswFileWatcher _watcher;
		#endif

	public:
		RetroPlugView(RetroPlugProject& project);
		~RetroPlugView() = default;

	void onInitialize() override;

		bool onDragMove(orb::DragContext& ctx, orb::Point position) override { return true; }

		bool onDrop(const std::vector<std::string>& paths) override;

		void onUpdate(f32 deltaTime) override;

		void onRender(orb::Canvas& canvas) override;

		bool onKey(const orb::KeyEvent& event) override;

		void setRootContainer(const std::shared_ptr<RootContainer>& container);

	private:
		void rebuildUi();

		entt::registry& getRegistry() {
			return _project.getRegistry();
		}
	};
}
