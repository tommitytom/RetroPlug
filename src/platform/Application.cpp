#include "Application.h"

#include <algorithm>

using namespace rp;

void Application::handleResize(int32 w, int32 h) {
	w = std::max(w, 0);
	h = std::max(h, 0);

	_resolution = { (uint32)w, (uint32)h };
	onResize(w, h);
}

void Application::handleKey(int key, int scancode, int action, int mods) {
	onKey(key, scancode, action, mods);
}

void Application::handleDrop(int count, const char** paths) {
	onDrop(count, paths);
}

void Application::handleMouseMove(double x, double y) {
	onMouseMove(x, y);
}

void Application::handleMouseButton(int button, int action, int mods) {
	onMouseButton(button, action, mods);
}

void Application::handleMouseScroll(double x, double y) {
	onMouseScroll(x, y);
}
