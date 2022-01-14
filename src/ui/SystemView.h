#pragma once

#include "TextureView.h"
#include "core/System.h"
#include "platform/Menu.h"

namespace rp {
	class SystemView final : public TextureView {
	private:
		SystemPtr _system;
		Image _frameBuffer;

	public:
		SystemView();
		~SystemView() {}

		void setSystem(SystemPtr& system) {
			_system = system;
			setDimensions(system->getResolution());
		}

		SystemPtr getSystem() {
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
