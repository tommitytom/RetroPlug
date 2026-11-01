// r8brain's static members (its filter, FFT and frac-bank caches), compiled here instead of linked from the vendored
// `r8brain` library.
//
// r8bbase.cpp is the only part of r8brain that is not a header, and it defines the objects every r8brain class shares.
// Built by the vendored target it would use r8brain's default memory hooks (`CStdClassAllocator`, `CStdMemAllocator`),
// while every other translation unit in this tree builds those same classes on `R8bAllocator` (R8bAllocator.hpp) - two
// definitions of each class in one program, of which the linker keeps one copy per symbol and no diagnostic says which.
// Including it after that header puts these statics on the allocator too, and neither this file's target
// (`retroplug-r8brain-base`) nor `retroplug-dsp-resample`, which links it, links the vendored library, so there is no
// second copy to keep. `retroplug_forbid_linking(r8brain)` in the root CMakeLists refuses a build that links it
// again.
#include "dsp/R8bAllocator.hpp"  // the memory hooks, which must be defined before r8brain's headers are read

#include <r8bbase.cpp>
