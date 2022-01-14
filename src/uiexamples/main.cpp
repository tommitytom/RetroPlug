#include "platform/Window.h"
#include "ExampleApplication.h"

using namespace rp;

ExampleApplication* app = nullptr;

int main() {
	{
		app = new ExampleApplication("UI Test", 320, 288);
		Window window(app);
		window.run();
		delete app;
	}

	return 0;
}
