// rp-device-trap-probe: the link trap's positive control (CMakeLists.txt). Built as rp-device-check is, it allocates
// with operator new and with malloc, so it must fail to link, naming this object for __wrap__Znwj and __wrap_malloc;
// tools/build-device-check.sh fails unless it does. Never an image.

#include <cstdlib>

int* volatile  gProbeNew;
void* volatile gProbeMalloc;

int main() {
    gProbeNew = new int(1);
    gProbeMalloc = std::malloc(16);
    return 0;
}
