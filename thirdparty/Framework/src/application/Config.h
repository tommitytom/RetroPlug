#pragma once

#ifdef FW_USE_GLFW
#include "application/GlfwNativeWindow.h"
namespace fw::app { using WindowManagerT = GlfwWindowManager; }
#else
#include "application/WindowManager.h"
namespace fw::app { using WindowManagerT = WindowManager; }
#endif
