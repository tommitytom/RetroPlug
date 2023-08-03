#pragma once

#include "application/Application.h"

namespace fw::ApplicationFactory {
	std::unique_ptr<fw::app::Application> create();
}
