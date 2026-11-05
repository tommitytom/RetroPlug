# An RP2350 image under the link trap (docs/modular-graph-phase-e1-design.md section 7, and the E2 design's section 8):
# the runtime with no heap, the trap on the heap, on exceptions and on RTTI, and the post-link check that the trap held.
# Included, after project() and pico_sdk_init() and before the first package, by both device projects:
# packages/graph-runtime/device-check (rp-device-check) and pico/n8-host (the box's firmware). RP_DEVICE_IMAGE names
# the including image in what this says. Defines rp_device_executable() and rp_device_trap_check().
#
# Included once a project: a second include (two images, or a sibling directory's) finds the removal below done and the
# trap's list written, and leaves them as they are. What the two functions need is kept in global properties, so they
# work from any directory of the project, not only the including one and its subdirectories.
include_guard(GLOBAL)
if(NOT RP_DEVICE_IMAGE)
    message(FATAL_ERROR "device-image.cmake: set RP_DEVICE_IMAGE (the image's name) before including it")
endif()

# --- the runtime: the SDK's pico_stdlib, with pico_malloc taken out of pico_runtime. pico_malloc is the one library
# of the SDK an image cannot have under the trap: it DEFINES the wrapped malloc family (its own --wrap, a heap behind
# it), so with it in the link a call to malloc resolves to its wrapper, the trap fires only further down, at newlib's
# malloc.o, naming that rather than the object that called it, and check-trap.cmake fails the build on the wrapper's
# definition. The rest of what these images link does under the full trap: stdio over the UART and pico_printf among
# them (the SDK wraps puts and printf to its own implementation, which allocates nothing), and stdio over USB too
# (TinyUSB's device CDC, scratch-built against SDK 2.1.1 with its submodules; neither image has it). That holds for
# the SDK's runtime, not for every library: the default alarm pool is static, but alarm_pool_create makes a pool on
# the heap, and a library that calls it (Pico-PIO-USB's host does, at init, unless its config skips the pool, and
# refers to it even then) links only once that reference leads elsewhere (pico/n8-host/CMakeLists.txt).
#
# It is taken out of the SDK's own target, in place, rather than out of a copy of it: every consumer of pico_runtime
# loses it, a library that links pico_stdlib itself (most third-party Pico libraries do) included, and pico_runtime
# keeps everything else it carries, its compile definitions among them. A pico_runtime that no longer links pico_malloc
# stops the configure, as the SDK has moved under it.
if(NOT PICO_SDK_VERSION_STRING STREQUAL "2.1.1")
    message(WARNING "${RP_DEVICE_IMAGE}: built and checked against Pico SDK 2.1.1, and this is "
                    "${PICO_SDK_VERSION_STRING}")
endif()
get_target_property(_rp_runtime_libraries pico_runtime INTERFACE_LINK_LIBRARIES)
if(NOT pico_malloc IN_LIST _rp_runtime_libraries)
    message(FATAL_ERROR "${RP_DEVICE_IMAGE}: pico_runtime no longer links pico_malloc (it links "
                        "${_rp_runtime_libraries}); the Pico SDK's runtime has changed, and what this image removes "
                        "from it must be read again")
endif()
list(REMOVE_ITEM _rp_runtime_libraries pico_malloc)
set_property(TARGET pico_runtime PROPERTY INTERFACE_LINK_LIBRARIES "${_rp_runtime_libraries}")

# --- the link trap. Every symbol through which code reaches the heap or throws is wrapped, and no __wrap_ symbol is
# defined, so a reference to any of them in what the image links is an undefined __wrap_ reference: a link error
# naming the object (and the function) that made it. Every object is compiled with a section per function, so
# --gc-sections (pico_standard_link) means it names what the image keeps, not every function an object happens to
# hold. The C heap is newlib's, and newlib reaches it through its reentrant _r forms as well as the plain ones, so both
# are listed, and sbrk, the break under it; operator new and delete, the C++ runtime's throw path and the
# std::__throw_* helpers the standard headers call are read from the device's own libstdc++ (every variant of each its
# multilib defines), so a variant is never missed for being spelt differently. The function-local static guards
# (__cxa_guard_*) are wrapped too: libsupc++ implements them with a throw, and a device image has no business with
# them (the device product's registration is written to need none).
#
# So is the runtime that exception handling and RTTI reference, so an object compiled with either (a vendored source,
# or one a library adds outside the SDK's -fno-exceptions -fno-rtti) is a link error even where it throws nothing:
# the personality routine and the catch entries a function with a handler or a cleanup references (on ARM a cleanup
# ends in __cxa_end_cleanup; _Unwind_Resume, its form elsewhere, is libgcc's and listed by name), __dynamic_cast, and
# the vtables of the three class type_info kinds a polymorphic class's RTTI points at. __cxa_pure_virtual is not
# trapped: an abstract class compiled without RTTI references it, and a device may have one.
set(_rp_trapped
    malloc calloc realloc free memalign aligned_alloc posix_memalign valloc pvalloc reallocf reallocarray
    malloc_usable_size
    _malloc_r _calloc_r _realloc_r _free_r _memalign_r _valloc_r _pvalloc_r _reallocf_r _malloc_usable_size_r
    sbrk _sbrk _sbrk_r
    __cxa_guard_acquire __cxa_guard_release __cxa_guard_abort
    _Unwind_Resume)
separate_arguments(_rp_cxx_flags UNIX_COMMAND "${CMAKE_CXX_FLAGS}")
execute_process(COMMAND ${CMAKE_CXX_COMPILER} ${_rp_cxx_flags} -print-file-name=libstdc++.a
                OUTPUT_VARIABLE _rp_libstdcxx OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT IS_ABSOLUTE "${_rp_libstdcxx}" OR NOT EXISTS "${_rp_libstdcxx}")
    message(FATAL_ERROR "${RP_DEVICE_IMAGE}: no libstdc++.a for these flags (${CMAKE_CXX_FLAGS}): \"${_rp_libstdcxx}\"")
endif()
# A toolchain upgraded in place re-runs the configure, so the list is read from the library the link uses.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_rp_libstdcxx}")
execute_process(COMMAND ${CMAKE_NM} -g --defined-only "${_rp_libstdcxx}" OUTPUT_VARIABLE _rp_nm)
set(_rp_cxx_names
    "_Znw[A-Za-z0-9_]+" "_Zna[A-Za-z0-9_]+"  # operator new and new[]
    "_Zdl[A-Za-z0-9_]+" "_Zda[A-Za-z0-9_]+"  # operator delete and delete[]
    "_ZSt[0-9]+__throw_[A-Za-z0-9_]+"        # std::__throw_*
    "__cxa_(throw|rethrow|allocate_exception|allocate_dependent_exception|free_exception|free_dependent_exception)"
    "__cxa_(init_primary_exception|throw_bad_array_new_length|bad_cast|bad_typeid)"
    # exception handling and RTTI; the type_info vtables are data (R), not code
    "__gxx_personality_v0" "__cxa_(begin_catch|end_catch|end_cleanup)" "__dynamic_cast"
    "_ZTVN10__cxxabiv1[0-9]+__(si_|vmi_)?class_type_infoE")
list(JOIN _rp_cxx_names "|" _rp_cxx_names)
string(REGEX MATCHALL "[ \t][TWR] (${_rp_cxx_names})\n" _rp_cxx "${_rp_nm}")
list(TRANSFORM _rp_cxx REPLACE "^[ \t][TWR] ([^\n]+)\n$" "\\1")
list(REMOVE_DUPLICATES _rp_cxx)
foreach(_rp_required _Znwj _ZdlPv __cxa_throw __cxa_allocate_exception _ZSt17__throw_bad_allocv
                     __gxx_personality_v0 __cxa_begin_catch __cxa_end_catch __cxa_end_cleanup __dynamic_cast
                     _ZTVN10__cxxabiv117__class_type_infoE _ZTVN10__cxxabiv120__si_class_type_infoE
                     _ZTVN10__cxxabiv121__vmi_class_type_infoE)
    if(NOT _rp_required IN_LIST _rp_cxx)
        message(FATAL_ERROR "${RP_DEVICE_IMAGE}: ${_rp_libstdcxx} defines no ${_rp_required}; the trap would miss it")
    endif()
endforeach()
list(APPEND _rp_trapped ${_rp_cxx})
list(LENGTH _rp_trapped _rp_trapped_count)
message(STATUS "${RP_DEVICE_IMAGE}: the link trap wraps ${_rp_trapped_count} symbols")
set(_rp_trap_list "${CMAKE_BINARY_DIR}/trapped-symbols.txt")
list(JOIN _rp_trapped "\n" _rp_trapped_lines)
file(WRITE "${_rp_trap_list}" "${_rp_trapped_lines}\n")
set_property(GLOBAL PROPERTY RP_DEVICE_TRAPPED "${_rp_trapped}")
set_property(GLOBAL PROPERTY RP_DEVICE_TRAP_LIST "${_rp_trap_list}")
set_property(GLOBAL PROPERTY RP_DEVICE_CHECK_TRAP "${CMAKE_CURRENT_LIST_DIR}/check-trap.cmake")

# --- the stack (the E3 design's section 7). Every function an image compiles, its own sources, the SDK's and the USB
# stacks' that build into it, and its packages' libraries, is held to a frame of at most RP_DEVICE_STACK_FRAME bytes:
# -Wstack-usage as an error, so a frame past it (or one GCC cannot bound, a variable-length array or an alloca) fails
# the build naming the function. That is the measure of a node's stack in query_memory and create that E1 asked for, in
# place of a hand sum: it bounds each frame, not a chain of them, and the chains are still the images' own word
# (pico/n8-host/CMakeLists.txt's for the box). 1 KiB is a quarter of the 4 KB main stack the box gives. The limit is
# on in every build type, and both the shipped Release (-O3) and the Debug Probe's Debug (-Og) builds of both images
# hold it: the deepest frames are bootRom's battery path (writeBattery, 864 bytes at -O3 and 840 at -Og, which the box
# never calls), sizePlan for a caller that asks for no error (792 at -O3), loadPlan (768 at -O3, 504 at -Og; its
# no-error path is a frame of its own, graph/PlanLoader.cpp) and a dither's or a normalize's create in rp-device-check
# (592). Inlining moves frames between the two: the loader's check() is 560 bytes at -Og and 344 at -O3. (A later
# -Werror=stack-usage on a command line replaces an earlier one, which is what the remedy below relies on.)
#
# A frame past the limit is fixed where it is ours: a large local moved into a frame of its own (RP_NOINLINE,
# graph/noinline.hpp) or off the stack. Where it is a vendored source's (a later SDK, TinyUSB or Pico-PIO-USB), the
# remedy is that one source's own limit, a COMPILE_OPTIONS property on it (set_source_files_properties) raising
# -Werror=stack-usage for it alone, beside a comment naming the function, its frame and why the chain still fits; never
# the flag taken off a target.
#
# A package's library takes the limit through RP_DEVICE_COMPILE_OPTIONS (cmake/packages.cmake's
# retroplug_package_library), which is why a project includes this file after setting those options and before it adds
# its first package; an image takes it in rp_device_executable().
set(RP_DEVICE_STACK_FRAME 1024)
get_property(_rp_packages_added GLOBAL PROPERTY RP_PACKAGES)
if(_rp_packages_added)
    message(FATAL_ERROR "${RP_DEVICE_IMAGE}: include device-image.cmake before the first retroplug_package(), so the "
                        "packages' libraries are held to the stack limit (${_rp_packages_added} are added already)")
endif()
if(NOT DEFINED RP_DEVICE_COMPILE_OPTIONS)
    message(FATAL_ERROR "${RP_DEVICE_IMAGE}: set RP_DEVICE_COMPILE_OPTIONS (the SDK's code-generation options) before "
                        "including device-image.cmake, which adds the stack limit to them")
endif()
list(APPEND RP_DEVICE_COMPILE_OPTIONS "-Werror=stack-usage=${RP_DEVICE_STACK_FRAME}")
set_property(GLOBAL PROPERTY RP_DEVICE_STACK_FRAME "${RP_DEVICE_STACK_FRAME}")

# rp_device_executable(<target> <sources>...): an executable as a device builds it: the device-mode flags, the stack
# limit, the runtime and the trap. One --wrap a symbol: -Wl, splits at commas, so a joined list would hand the linker
# the symbols as input files.
function(rp_device_executable target)
    add_executable(${target} ${ARGN})
    set_target_properties(${target} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
    target_compile_definitions(${target} PRIVATE RP_DEVICE)
    get_property(_frame GLOBAL PROPERTY RP_DEVICE_STACK_FRAME)
    target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions;-fno-rtti>"
                           "-Werror=stack-usage=${_frame}")
    target_link_libraries(${target} PRIVATE pico_stdlib)
    get_property(_trapped GLOBAL PROPERTY RP_DEVICE_TRAPPED)
    list(TRANSFORM _trapped PREPEND "LINKER:--wrap=" OUTPUT_VARIABLE _wrap)
    target_link_options(${target} PRIVATE ${_wrap})
endfunction()

# rp_device_trap_check(<target>): the trap's second half on <target>'s linked image. None of the wrapped symbols may be
# defined (a library that defined one, as pico_malloc does the malloc family's, would turn the trap into a heap), and
# none may be referred to and left undefined (a link option that tolerates one). Both are checked on the linked image,
# which fails the build when either holds.
function(rp_device_trap_check target)
    get_property(_trap_list GLOBAL PROPERTY RP_DEVICE_TRAP_LIST)
    get_property(_check_trap GLOBAL PROPERTY RP_DEVICE_CHECK_TRAP)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -DNM=${CMAKE_NM} -DIMAGE=$<TARGET_FILE:${target}> -DTRAPPED=${_trap_list}
                -P "${_check_trap}"
        VERBATIM)
endfunction()
