# The throwing variant, packfixture-throws, in a directory of its own (the parent's _rp_fixture_variant does the rest).
# Its generated extern "C" entry calls FixtureThrows.cpp's prelude, which throws. MSVC compiles this directory with
# /EHs rather than the tree's /EHsc, so the entry is not assumed never to throw and the exception unwinds through it
# to the loader's handler: a per-source /EHs would be a second /EH option on one command line, D9025, which cannot be
# suppressed, and a directory's CMAKE_CXX_FLAGS is its own.
if(MSVC)
    string(REPLACE "/EHsc" "/EHs" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()
_rp_fixture_variant(packfixture-throws SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/FixtureThrows.cpp ARGS THROWING_ENTRY)
