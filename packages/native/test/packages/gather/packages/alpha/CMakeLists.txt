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
elseif(RP_GATHER_CASE STREQUAL "unjudged")
    # A generator expression only the build generator resolves: refused rather than passed unseen.
    target_link_libraries(retroplug-alpha INTERFACE
        $<TARGET_PROPERTY:retroplug-beta,INTERFACE_INCLUDE_DIRECTORIES>)
endif()
