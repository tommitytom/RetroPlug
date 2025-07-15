#pragma once

#include "graphics/Texture.h"
#include "ui/GridItem.h"
#include "ui/Menu.h"
#include "ui/View.h"
#include "core/System.h"

namespace rp {
	class SystemView final : public GridItem {
		FwRegisterObject();
	private:
		SystemPtr _system;
		fw::RectF _textureArea;
		fw::TextureHandle _texture;
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

		void setSystem(const SystemPtr& system) {
			_system = system;
			getLayout().setDimensions((fw::Dimension)system->getResolution());
		}

		const SystemPtr& getSystem() {
			return _system;
		}

		void onInitialize() override {
			getLayout().setOverflow(fw::FlexOverflow::Hidden);
		}

		bool onDrop(const std::vector<std::string>& paths) override;

		void onUpdate(f32 delta) override;

		void onRender(fw::Canvas& canvas) override;

		uint32 getVersion() const {
			return _version;
		}

		void processInput(std::vector<fw::StreamButtonPress>& stream, std::vector<std::string>& actions) override;

		void createMenu(fw::Menu& menu) override;
	};

	using SystemViewPtr = std::shared_ptr<SystemView>;
}
