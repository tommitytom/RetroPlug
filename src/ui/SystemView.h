#pragma once

#include "TextureView.h"
#include "core/SystemWrapper.h"
#include "platform/Menu.h"

namespace rp {
	class SystemView final : public TextureView {
	private:
		SystemWrapperPtr _system;
		Image _frameBuffer;

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
			setDimensions((Dimension)system->getSystem()->getResolution());
		}

		SystemWrapperPtr getSystem() {
			return _system;
		}

		bool onDrop(const std::vector<std::string>& paths) override;

		bool onKey(VirtualKey::Enum key, bool down) override;

		bool onButton(ButtonType::Enum button, bool down) override;

		void onUpdate(f32 delta) override;

		const Image& getFrameBuffer() const {
			return _frameBuffer;
		}

		uint32 getVersion() const {
			return _version;
		}

	private:
		void buildMenu(Menu& target);
	};

	using SystemViewPtr = std::shared_ptr<SystemView>;
}
