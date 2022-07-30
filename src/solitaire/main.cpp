#include "Application.h"
#include "Game.h"

#include <spdlog/spdlog.h>

using namespace rp;

static uint32 count = 0;

class BasicWindow : public rp::app::Window {
private:
	entt::resource<engine::Texture> _tex;

public:
	BasicWindow() : rp::app::Window(fmt::format("Basic window {}", count++), { 300, 300 }) {}
	~BasicWindow() = default;

	void onInitialize() override {
		_tex = getCanvas().loadTexture("cardback.png");
	}

	void onFrame(f32 delta) override {
		//getCanvas().setTransform(Mat3x3::rotation(1.5f)).fillRect(Rect{ 0, 0, 100, 100 }, Color4F(0.5, 0, 0, 1));
		getCanvas().fillRect(Rect{ 0, 0, 100, 100 }, Color4F(1, 1, 1, 1));
		//getCanvas().texture(_tex, RectF(0, 0, 100, 100), Color4F(0, 0, 1, 1));
	}

	void onKey(VirtualKey::Enum key, bool down) override {
		if (key == VirtualKey::Space && down) {
			getWindowManager().createWindow<BasicWindow>();
		}
	}
};

int main(void) {
	rp::app::Application app;
	return app.run<BasicWindow>();
	//return app.run<rp::Game>();
}
