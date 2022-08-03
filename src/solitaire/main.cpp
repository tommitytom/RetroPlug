#include "application/Application.h"
#include "Game.h"

#include <spdlog/spdlog.h>

using namespace rp;

static uint32 count = 0;

class BasicWindow : public View {
private:
	entt::resource<engine::Texture> _tex;

public:
	BasicWindow(): View({ 300, 300 }) {
		setType<BasicWindow>();
		setName(fmt::format("Basic window {}", count++));
	}

	~BasicWindow() = default;

	void onInitialize() override {
		//_tex = getCanvas().loadTexture("cardback.png");
	}

	void onRender(engine::Canvas& canvas) override {
		canvas.fillRect(Rect{ 10, 10, 100, 100 }, Color4F(1, 1, 1, 1));
	}

	bool onKey(VirtualKey::Enum key, bool down) override {
		if (key == VirtualKey::Space && down) {
			//getWindowManager().createWindow<BasicWindow>();
		}

		return true;
	}
};

int main(void) {
	rp::app::Application app;
	//return app.run<BasicWindow>();
	return app.run<rp::Game>();
}
