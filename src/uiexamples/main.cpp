#include "platform/Window.h"
#include "ExampleApplication.h"

using namespace rp;

ExampleApplication* app = nullptr;

int main() {
	{
		app = new ExampleApplication("UI Test", 800, 600);
		Window window(app);
		window.run();
		delete app;
	}

	return 0;
}
