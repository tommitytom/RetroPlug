#include "application/Application.h"
#include "Scene.h"

int main(void) {
	rp::app::Application app;
	return app.run<rp::Scene>();
}
