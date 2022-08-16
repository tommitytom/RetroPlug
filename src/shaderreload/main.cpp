#include "application/Application.h"
#include "ShaderReloadWindow.h"

using namespace rp;

int main(void) {
	return rp::app::Application::run<rp::ShaderReloadWindow>();
}
