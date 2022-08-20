#include "foundation/MacroTools.h"

#include "application/Application.h"
#include INCLUDE_EXAMPLE(EXAMPLE_IMPL)

using namespace rp;

int main() {
	return app::Application::run<EXAMPLE_IMPL>();
}
