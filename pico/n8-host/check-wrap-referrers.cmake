# The box's second half of the alarm-pool wrap (CMakeLists.txt, SofTimer.cpp): run on the linked image's map, it fails
# the build unless Pico-PIO-USB's pio_usb_host.c is the one object that refers to __wrap_alarm_pool_create_on_timer.
# --wrap=alarm_pool_create_on_timer points every call at a stub that panics, which takes that symbol out of the link
# trap's reach: any other caller (a library added later, a Pico-PIO-USB checkout that calls the pool elsewhere) would
# link cleanly and panic at boot where the trap would have failed the link naming its object. The map's cross reference
# table (LINKER:--cref) lists a symbol's defining file, then each file that refers to it.
#
# cmake -DMAP=<the image's map> -DSYMBOL=<symbol> -DREFERRER=<the one object's name> -P check-wrap-referrers.cmake
file(READ "${MAP}" _map)
string(FIND "${_map}" "Cross Reference Table" _table)
if(_table EQUAL -1)
    message(FATAL_ERROR "check-wrap-referrers: ${MAP} has no cross reference table (the link needs --cref)")
endif()
string(SUBSTRING "${_map}" ${_table} -1 _map)
string(REGEX MATCH "\n${SYMBOL}[ \t]+[^\n]*\n(([ \t]+[^ \t\n][^\n]*\n)*)" _entry "${_map}")
if(NOT _entry)
    message(FATAL_ERROR "check-wrap-referrers: ${MAP} lists no ${SYMBOL}: the wrap is no longer in the link")
endif()
string(REGEX MATCHALL "[^ \t\n]+" _referrers "${CMAKE_MATCH_1}")
list(LENGTH _referrers _count)
set(_ok FALSE)
if(_count EQUAL 1)
    list(GET _referrers 0 _only)
    if(_only MATCHES "(^|/)${REFERRER}$")
        set(_ok TRUE)
    endif()
endif()
if(NOT _ok)
    list(JOIN _referrers "\n  " _referrers)
    message(FATAL_ERROR "check-wrap-referrers: ${SYMBOL} must be referred to by ${REFERRER} alone (the skipped "
                        "alarm_pool_create in Pico-PIO-USB's init), and ${MAP} lists:\n  ${_referrers}\nAnother "
                        "caller of alarm_pool_create would panic at boot instead of failing the link trap.")
endif()
