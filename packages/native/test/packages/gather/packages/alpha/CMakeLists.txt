add_library(retroplug-alpha INTERFACE)
if(RP_GATHER_CASE STREQUAL "later")
    target_link_libraries(retroplug-alpha INTERFACE retroplug-beta-helper)
elseif(RP_GATHER_CASE STREQUAL "shell")
    target_link_libraries(retroplug-alpha INTERFACE retroplug-backend)
elseif(RP_GATHER_CASE STREQUAL "dev-library")
    target_link_libraries(retroplug-alpha INTERFACE retroplug-testing)
elseif(RP_GATHER_CASE STREQUAL "declared")
    target_link_libraries(retroplug-alpha INTERFACE retroplug-beta)
elseif(RP_GATHER_CASE STREQUAL "test-dev")
    target_link_libraries(retroplug-alpha INTERFACE retroplug-testing)
    set_target_properties(retroplug-alpha PROPERTIES RP_TEST_BINARY ON)
elseif(RP_GATHER_CASE STREQUAL "genex")
    # A generator expression around a target name is judged like the name: beta's helper, which alpha does not declare.
    target_link_libraries(retroplug-alpha INTERFACE $<BUILD_INTERFACE:retroplug-beta-helper>)
elseif(RP_GATHER_CASE STREQUAL "genex-declared")
    # Both forms a package actually writes, and beta, which alpha declares: the check evaluates the names and holds.
    target_link_libraries(retroplug-alpha INTERFACE $<LINK_ONLY:retroplug-beta> $<BUILD_INTERFACE:retroplug-beta-helper>)
    target_include_directories(retroplug-alpha INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../beta/native/src>)
elseif(RP_GATHER_CASE STREQUAL "interface-include")
    # What a consumer of the target adds, not the target's own directory: still another package's tree.
    target_include_directories(retroplug-alpha INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/../beta/native/src)
elseif(RP_GATHER_CASE STREQUAL "source-outside")
    # A source of beta's compiled into alpha's target: the same boundary as an include directory.
    target_sources(retroplug-alpha INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/../beta/native/src/x.cpp)
elseif(RP_GATHER_CASE STREQUAL "alias")
    # An alias is not a target of its own: the check that attributes a target to a package reads the target behind it.
    target_link_libraries(retroplug-alpha INTERFACE rp::beta)
elseif(RP_GATHER_CASE STREQUAL "alias-declared")
    # The same alias with beta declared: resolved to beta's target, and held.
    target_link_libraries(retroplug-alpha INTERFACE rp::beta)
elseif(RP_GATHER_CASE STREQUAL "testing-dependency")
    # The test helpers linked by a library, which is no test binary: the boundary check allows a DECLARED dependency, and
    # the test-helper guard is what refuses this one.
    target_link_libraries(retroplug-alpha INTERFACE retroplug-testing)
elseif(RP_GATHER_CASE STREQUAL "testing-allocator")
    # The allocator target alone, which the guard refuses to a non-test target for the same reason.
    target_link_libraries(retroplug-alpha INTERFACE retroplug-testing-allocator)
elseif(RP_GATHER_CASE STREQUAL "test-dev-allocator")
    # The allocator from a test binary, which the guard allows: the no-allocation checks are exactly such a binary.
    target_link_libraries(retroplug-alpha INTERFACE retroplug-testing-allocator)
    set_target_properties(retroplug-alpha PROPERTIES RP_TEST_BINARY ON)
elseif(RP_GATHER_CASE STREQUAL "defs" OR RP_GATHER_CASE STREQUAL "defs-dev"
       OR RP_GATHER_CASE STREQUAL "defs-quoted" OR RP_GATHER_CASE STREQUAL "defs-dev-quoted")
    # A file opened by the path a package's manifest gives: a compile definition is a link by another name, judged like
    # one. A test target (RP_TEST_BINARY) may reach a devDependency's, which is what defs-dev declares. The -quoted pair
    # writes the value as every DEFS in the tree does, NAME="${...}", which keeps the quotes in the property.
    if(RP_GATHER_CASE MATCHES "-quoted$")
        target_compile_definitions(retroplug-alpha INTERFACE
            RP_ROM_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../beta/native/src/rom.gb")
    else()
        target_compile_definitions(retroplug-alpha INTERFACE
            RP_ROM_PATH=${CMAKE_CURRENT_SOURCE_DIR}/../beta/native/src/rom.gb)
    endif()
    set_target_properties(retroplug-alpha PROPERTIES RP_TEST_BINARY ON)
elseif(RP_GATHER_CASE STREQUAL "comment-only")
    # This file's text names beta's file - ${CMAKE_CURRENT_SOURCE_DIR}/../beta/native/src/rom.gb - and no target
    # property does: the gatherer reads the target's properties, so a comment is not a reach (the TS checker strips
    # them for the same reason).
elseif(RP_GATHER_CASE STREQUAL "copt")
    # An include directory smuggled into the compile options: judged like one its include property would name.
    target_compile_options(retroplug-alpha INTERFACE -I${CMAKE_CURRENT_SOURCE_DIR}/../beta/native/src)
elseif(RP_GATHER_CASE STREQUAL "copt-genex")
    # A condition around a literal is the literal: the language expression says WHEN, not what.
    target_compile_options(retroplug-alpha INTERFACE
        $<$<COMPILE_LANGUAGE:CXX>:-I${CMAKE_CURRENT_SOURCE_DIR}/../beta/native/src>)
elseif(RP_GATHER_CASE STREQUAL "copt-relative")
    # A relative -I names no directory the check can attribute: the compiler resolves it against the build generator's
    # working directory (Ninja's is the build root, Makefiles' the target's binary dir), never this target's SOURCE_DIR.
    target_compile_options(retroplug-alpha INTERFACE -I../beta/native/src)
elseif(RP_GATHER_CASE STREQUAL "copt-iquote")
    target_compile_options(retroplug-alpha INTERFACE -iquote${CMAKE_CURRENT_SOURCE_DIR}/../beta/native/src)
elseif(RP_GATHER_CASE STREQUAL "copt-idirafter")
    target_compile_options(retroplug-alpha INTERFACE -idirafter${CMAKE_CURRENT_SOURCE_DIR}/../beta/native/src)
elseif(RP_GATHER_CASE STREQUAL "copt-isystem")
    target_compile_options(retroplug-alpha INTERFACE -isystem${CMAKE_CURRENT_SOURCE_DIR}/../beta/native/src)
elseif(RP_GATHER_CASE STREQUAL "copt-external")
    # MSVC's spelling of -isystem.
    target_compile_options(retroplug-alpha INTERFACE /external:I${CMAKE_CURRENT_SOURCE_DIR}/../beta/native/src)
elseif(RP_GATHER_CASE STREQUAL "copt-shell")
    # CMake's SHELL: form of the separated flag, which is one item with a space in it.
    target_compile_options(retroplug-alpha INTERFACE "SHELL:-I ${CMAKE_CURRENT_SOURCE_DIR}/../beta/native/src")
elseif(RP_GATHER_CASE STREQUAL "link-path")
    # A library linked by its file path: where it lies decides, as the target it comes from would.
    target_link_libraries(retroplug-alpha INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/../beta/native/src/libretroplug-beta.a)
elseif(RP_GATHER_CASE STREQUAL "link-path-shell")
    target_link_libraries(retroplug-alpha INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/../native/src/libretroplug-backend.a)
elseif(RP_GATHER_CASE STREQUAL "link-options-dir")
    # A search directory in beta's tree lets the linker find beta's library.
    target_link_options(retroplug-alpha INTERFACE -L${CMAKE_CURRENT_SOURCE_DIR}/../beta/lib)
elseif(RP_GATHER_CASE STREQUAL "link-options-name")
    # A library name the build's own target of that name attributes to beta.
    target_link_options(retroplug-alpha INTERFACE -lretroplug-beta)
elseif(RP_GATHER_CASE STREQUAL "link-options-name-shell")
    target_link_options(retroplug-alpha INTERFACE -lretroplug-backend)
elseif(RP_GATHER_CASE STREQUAL "includes-target-property")
    # beta's include directories named through a generator expression: a link to beta in everything but spelling, so
    # the target named decides (docs/modular-graph-phase-f5a-design.md section 6.7's generic rule).
    target_include_directories(retroplug-alpha INTERFACE
        $<TARGET_PROPERTY:retroplug-beta,INTERFACE_INCLUDE_DIRECTORIES>)
elseif(RP_GATHER_CASE STREQUAL "objects" OR RP_GATHER_CASE STREQUAL "objects-declared")
    # Another target's objects, named by a generator expression: what they compile is that target's package's, judged
    # as a link is (never as a source of alpha's own directory).
    target_sources(retroplug-alpha INTERFACE $<TARGET_OBJECTS:retroplug-beta-objects>)
elseif(RP_GATHER_CASE STREQUAL "unjudged")
    # A generator expression only the build generator resolves: refused rather than passed unseen.
    target_link_libraries(retroplug-alpha INTERFACE
        $<TARGET_PROPERTY:retroplug-beta,INTERFACE_COMPILE_DEFINITIONS>)
endif()
