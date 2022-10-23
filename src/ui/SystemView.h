#pragma once

#include "ui/TextureView.h"
#include "core/SystemWrapper.h"
#include "ui/Menu.h"

namespace rp {
	class SystemView final : public fw::TextureView {
	private:
		SystemWrapperPtr _system;
		fw::Image _frameBuffer;

		uint32 _version = 0;

	public:
		SystemView();
		~SystemView() {}

		bool versionIsDirty() const {
			return _system->getVersion() != _version;
		}

		void updateVersion() {
			_version = _system->getVersion();
		}

		void setSystem(SystemWrapperPtr& system) {
			_system = system;
			setDimensions((fw::Dimension)system->getSystem()->getResolution());
		}

		SystemWrapperPtr getSystem() {
			return _system;
		}

		bool onDrop(const std::vector<std::string>& paths) override;

		bool onKey(VirtualKey::Enum key, bool down) override;

		bool onButton(ButtonType::Enum button, bool down) override;

		void onUpdate(f32 delta) override;

		void onRender(Canvas& canvas) override {
 			TextureView::onRender(canvas);
		}

		const fw::Image& getFrameBuffer() const {
			return _frameBuffer;
		}

		uint32 getVersion() const {
			return _version;
		}

	private:
		void buildMenu(fw::Menu& target);
	};

	using SystemViewPtr = std::shared_ptr<SystemView>;
}
