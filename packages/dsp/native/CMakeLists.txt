# The DSP package (docs/modular-graph-phase-d1-design.md sections 4 and 5, docs/modular-graph-phase-e1-design.md
# section 4): the kernels (src/dsp/, with util/Hash.hpp, the FNV-1a a dither's seed is drawn from) and the rp.dsp and
# rp.dsp.resample node types around them. Added by retroplug_package(dsp) (cmake/packages.cmake).

# --- retroplug-dsp-kernels: the DSP the rp.dsp nodes run: gain and peak, the RBJ biquads, dither / quantise, and risa's
# DMC encoder (a whole-buffer kernel, Dpcm.hpp says why; the others stream a block at a time). A kit compile is a chain
# of those nodes, so it reaches this DSP through them.
# Core-free on purpose: it links the C++ standard library and nothing else, so retroplug-graph-test and the rp.dsp node
# package can use it with no core, txiki, backend or r8brain.
# (Not `retroplug-dsp`: dpf_add_plugin(retroplug) already names the plugin's own DSP library that.)
#
# The resampler is its own target on purpose, because its kernel is the one that is not freestanding: r8brain brings
# the heap, pthreads (its filter cache is a std::mutex) and RTTI (its resamplers are virtual). A build that has no use
# for it - the Pico's, which runs rp.dsp's gain / biquad / dither / normalize alone - omits the target and never sees
# any of that, and the rp.dsp package's resample node, which is the only node that needs it, is a target of its own for
# the same reason.
retroplug_package_library(retroplug-dsp-kernels
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/Gain.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/Biquad.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/Dither.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/Dpcm.cpp)
target_include_directories(retroplug-dsp-kernels PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# r8brain's static members are vendored code, so they get a target of their own rather than riding in one of ours:
# /W4 /WX is scoped to RetroPlug's own targets (packages/native/cmake/warnings.cmake), which this one is deliberately
# not on, and GNU/Clang get the same `-w` a vendored dep gets (retroplug_silence_dep_warnings). Its one source includes
# r8brain's r8bbase.cpp through dsp/R8bAllocator.hpp, so every r8brain class in the program is built on the throwing
# allocator; one translation unit in one target means there is no second definition of those classes to keep, and the
# tree declares no target for the vendored library, so there is none to link.
retroplug_package_library(retroplug-r8brain-base ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/R8bBase.cpp)
target_include_directories(retroplug-r8brain-base PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/deps/r8brain)
retroplug_silence_dep_warnings(retroplug-r8brain-base)

# Threads: r8brain's filter cache is a std::mutex. Found here as well as in packages/native, as an imported target is
# visible only in the directory that finds it and below.
find_package(Threads REQUIRED)
retroplug_package_library(retroplug-dsp-resample ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/Resample.cpp)
target_include_directories(retroplug-dsp-resample
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
    # r8brain's headers, all this build takes from the vendored dep, stay private to the translation units that include
    # dsp/R8bAllocator.hpp first: that header is what puts r8brain's classes on the throwing allocator, and a TU that
    # read them without it would build them on r8brain's defaults.
    PRIVATE ${CMAKE_SOURCE_DIR}/deps/r8brain)
# The tree declares no target for the vendored `r8brain` library. Its r8bbase.cpp would build r8brain's static members
# on r8brain's own malloc hooks, where src/dsp/R8bBase.cpp (in retroplug-r8brain-base) builds them on the throwing
# allocator the rest of this tree uses, and the linker would keep one copy of every r8brain class with no diagnostic
# saying which.
target_link_libraries(retroplug-dsp-resample PRIVATE Threads::Threads retroplug-r8brain-base)

# --- retroplug-dsp-nodes: the rp.dsp package (src/graph/nodes/DspNodes.cpp) - gain, biquad, dither, normalize and dpcm,
# each a node around the kernels above (docs/modular-graph-phase-d1-design.md section 5). A target of its own rather
# than a source of retroplug-graph: retroplug-graph is what every consumer of the engine links (the plugin, the CLI, the
# test hosts), and the package brings the DSP library with it, which retroplug-graph has no business carrying.
# It links retroplug-dsp-kernels alone, never retroplug-dsp-resample, so nothing here reaches r8brain, pthreads or
# RTTI, and it stays core-free like the other packages (no core, txiki, efsw or LVGL): retroplug-graph-test plans and
# runs these nodes with only the graph library and the kernels.
retroplug_package_library(retroplug-dsp-nodes
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/nodes/DspNodes.cpp)
target_include_directories(retroplug-dsp-nodes PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(retroplug-dsp-nodes PUBLIC retroplug-node-sdk PRIVATE retroplug-dsp-kernels)

# --- retroplug-dsp-resample-nodes: the rp.dsp.resample package (src/graph/nodes/DspResampleNodes.cpp), the resampler
# as a node, apart from retroplug-dsp-nodes because its kernel is retroplug-dsp-resample: r8brain, and with it the
# heap, pthreads and RTTI. A build for a target that cannot take those (the Pico's) links retroplug-dsp-nodes and
# retroplug-dsp-kernels alone and leaves this package's second entry, rp_dsp_resample_package, out of its registration
# list (the manifest marks it not `device`), so neither this node nor r8brain reaches its binary; the planner's refusal
# of a `heap` node there is the second line, not the only one.
retroplug_package_library(retroplug-dsp-resample-nodes
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graph/nodes/DspResampleNodes.cpp)
target_include_directories(retroplug-dsp-resample-nodes PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(retroplug-dsp-resample-nodes PUBLIC retroplug-node-sdk PRIVATE retroplug-dsp-resample)
