# The box's second half of the alarm-pool wrap (CMakeLists.txt, SofTimer.cpp): run on the linked image's map, it fails
# the build unless Pico-PIO-USB's pio_usb_host.c is the one object that refers to __wrap_alarm_pool_create_on_timer.
# --wrap=alarm_pool_create_on_timer points every call at a stub that panics, which takes that symbol out of the link
# trap's reach: any other caller (a library added later, a Pico-PIO-USB checkout that calls the pool elsewhere) would
# link cleanly and panic at boot where the trap would have failed the link naming its object. The map's cross reference
# table (LINKER:--cref) lists a symbol's defining file, then each file that refers to it.
#
# cmake -DMAP=<the image's map> -DSYMBOL=<symbol> -DREFERRER=<the one object's file name> -P check-wrap-referrers.cmake
# (REFERRER is a plain file name, such as pio_usb_host.c.o, or .c.obj where the host's objects take that suffix.) The
# map may end its lines in CRLF, and an object's path may hold spaces: one indented line is one referrer.
file(READ "${MAP}" _map)
string(REPLACE "\r" "" _map "${_map}")
string(FIND "${_map}" "Cross Reference Table" _table)
if(_table EQUAL -1)
    message(FATAL_ERROR "check-wrap-referrers: ${MAP} has no cross reference table (the link needs --cref)")
endif()
string(SUBSTRING "${_map}" ${_table} -1 _map)
string(REGEX MATCH "\n${SYMBOL}[ \t]+[^\n]*\n(([ \t]+[^ \t\n][^\n]*\n)*)" _entry "${_map}")
if(NOT _entry)
    message(FATAL_ERROR "check-wrap-referrers: ${MAP} lists no ${SYMBOL}: the wrap is no longer in the link")
endif()
set(_lines "${CMAKE_MATCH_1}")
string(REGEX REPLACE "(^|\n)[ \t]+" "\\1" _lines "${_lines}")
string(REGEX REPLACE "\n$" "" _lines "${_lines}")
set(_referrers "")
if(NOT _lines STREQUAL "")
    string(REPLACE "\n" ";" _referrers "${_lines}")
endif()
list(LENGTH _referrers _count)
string(REGEX REPLACE "([.+*?^$()\\[\\]|])" "\\\\\\1" _escaped "${REFERRER}")
set(_ok FALSE)
if(_count EQUAL 1)
    list(GET _referrers 0 _only)
    if(_only MATCHES "(^|/)${_escaped}$")
        set(_ok TRUE)
    endif()
endif()
if(NOT _ok)
    list(JOIN _referrers "\n  " _referrers)
    message(FATAL_ERROR "check-wrap-referrers: ${SYMBOL} must be referred to by ${REFERRER} alone (the skipped "
                        "alarm_pool_create in Pico-PIO-USB's init), and ${MAP} lists:\n  ${_referrers}\nAnother "
                        "caller of alarm_pool_create would panic at boot instead of failing the link trap.")
endif()
