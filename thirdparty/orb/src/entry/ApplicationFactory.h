#pragma once

#include "application/Application.h"

namespace orb::ApplicationFactory {
	std::unique_ptr<orb::app::Application> create();
}
