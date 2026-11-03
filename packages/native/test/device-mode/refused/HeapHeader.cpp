// Must not compile in device mode (cmake/tests.cmake checks it at configure): a heap header before the SDK.
#include <string>

#include "graph/node.hpp"

int main() { return 0; }
