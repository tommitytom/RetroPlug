#include "foundation/MacroTools.h"
#include "application/ApplicationRunner.h"

#ifndef APPLICATION_HEADER
#define APPLICATION_HEADER APPLICATION_IMPL
#endif

#include INCLUDE_APPLICATION(APPLICATION_HEADER)

using namespace fw;

int main() {
	return app::ApplicationRunner::run<APPLICATION_IMPL>();
}
