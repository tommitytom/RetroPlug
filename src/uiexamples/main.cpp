#include "application/Application.h"
#include "ExampleApplication.h"

using namespace rp;

int main() {
	return rp::app::Application::run<ExampleApplication>();
}
