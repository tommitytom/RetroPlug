// Holds the generated table's `count = 0` branch (cmake/packages.cmake) and the registration beside it: a desktop product
// whose packages embed nothing answers a count of 0 and a null table, and still lists its one package. Run at configure
// by packages/native/cmake/tests.cmake, so the branch is compiled and executed, not merely generated.
#include <cstddef>

#include "graph/Packages.hpp"

extern "C" const rp_package* rp_fixture_entry(void);

int main() {
    std::size_t resources = 12345;
    if (rp_embedded_resources(&resources) != nullptr) return 1;
    if (resources != 0) return 2;

    std::size_t packages = 0;
    const rp_package* const* list = rp_register_packages(&packages);
    if (list == nullptr || packages != 1 || list[0] != rp_fixture_entry()) return 3;
    return 0;
}
