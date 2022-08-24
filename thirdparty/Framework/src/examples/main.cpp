#include "foundation/MacroTools.h"

#include "application/Application.h"
#include INCLUDE_EXAMPLE(EXAMPLE_IMPL)

using namespace fw;

int main() {
	return app::Application::run<EXAMPLE_IMPL>();
}
