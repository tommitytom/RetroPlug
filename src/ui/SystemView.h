#pragma once

#include "ui/TextureView.h"
#include "core/System.h"
#include "ui/Menu.h"

namespace rp {
	class SystemView final : public fw::TextureView {
	private:
		SystemPtr _system;
		//fw::Image _frameBuffer;

		uint32 _version = 0;

	public:
		SystemView();
		~SystemView() {}

		bool versionIsDirty() const {
			return false;
			//return _system->getVersion() != _version;
		}

		void updateVersion() {
			//_version = _system->getVersion();
		}

		void setSystem(SystemPtr& system) {
			_system = system;
			setDimensions((fw::Dimension)system->getResolution());
		}

		SystemPtr getSystem() {
			return _system;
		}

		bool onDrop(const std::vector<std::string>& paths) override;

		bool onKey(const fw::KeyEvent& ev) override;

		bool onButton(const fw::ButtonEvent& ev) override;

		void onUpdate(f32 delta) override;

		void onRender(Canvas& canvas) override {
 			TextureView::onRender(canvas);
		}

		/*const fw::Image& getFrameBuffer() const {
			return _frameBuffer;
		}*/

		uint32 getVersion() const {
			return _version;
		}

	private:
		void buildMenu(fw::Menu& target);
	};

	using SystemViewPtr = std::shared_ptr<SystemView>;
}
