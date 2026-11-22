// The throwing variant's prelude (throws/CMakeLists.txt): the C++ function its generated extern "C" entry calls first
// (cmake/packs.cmake's THROWING_ENTRY). The throw is the fixture's whole point: the loader must catch it on its side of
// the C entry and refuse the pack. It is thrown here, with C++ linkage, because a throw written in an extern "C"
// dllexport'ed function is MSVC's C4297.

#include <stdexcept>

void rp_pack_entry_prelude();

void rp_pack_entry_prelude() { throw std::runtime_error("this pack's entry throws"); }
