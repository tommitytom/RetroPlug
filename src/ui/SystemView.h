#pragma once

#include "TextureView.h"
#include "core/SystemWrapper.h"
#include "platform/Menu.h"

namespace rp {
	class SystemView final : public TextureView {
	private:
		SystemWrapperPtr _system;
		Image _frameBuffer;

	public:
		SystemView();
		~SystemView() {}

		void setSystem(SystemWrapperPtr& system) {
			_system = system;
			setDimensions(system->getSystem()->getResolution());
		}

		SystemWrapperPtr getSystem() {
			return _system;
		}

		bool onKey(VirtualKey::Enum key, bool down) override;

		bool onButton(ButtonType::Enum button, bool down) override;

		void onUpdate(f32 delta) override;

		const Image& getFrameBuffer() const {
			return _frameBuffer;
		}

	private:
		void buildMenu(Menu& target);
	};

	using SystemViewPtr = std::shared_ptr<SystemView>;
}
