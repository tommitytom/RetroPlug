# Copyright (c) 2015-2022  Made to Order Software Corp.  All Rights Reserved
#
# https://snapwebsites.org/project/csspp
# contact@m2osw.com
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA

##
## Documentation
##
find_package(SnapDoxygen)
AddDoxygenTarget(
    csspp
    ${CSSPP_VERSION_MAJOR}
    ${CSSPP_VERSION_MINOR}
    ${CSSPP_VERSION_PATCH}
)

# At this point this may not work on all platforms, so to make it easier
# I specialize it to Linux only; that just means one doxygen \include
# will fail.
if(DOXYGEN_FOUND)
    if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
        add_dependencies(csspp_Documentation
            sample.css
        )
    
        set(CSSPP_TOOL csspp-tool)
    
        add_custom_target(sample.css
            DEPENDS ${CSSPP_TOOL}
            COMMAND ${CSSPP_TOOL}
                -I ${CMAKE_SOURCE_DIR}/scripts ${CMAKE_BINARY_DIR}/scripts
                --debug --style expanded --output sample.css
                ${CMAKE_CURRENT_SOURCE_DIR}/sample.scss
        )
        #
        # Somehow cmake doesn't recognize the CSSPP_TOOL variable if it's
        # not the very first variable on the command line!?
        #
        #   COMMAND ${CSSPP_TOOL}
        #
        # Commands when -fsanitize=... is in effect in a build
        # (It is not required to have these to build locally, only to build
        # on a build system with the debuild environment.)
        #
        #   COMMAND ldd ${CMAKE_BINARY_DIR}/src/csspp
        #   COMMAND LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libasan.so.2 ${CMAKE_BINARY_DIR}/src/csspp
        #
        # Note: the ldd command is used to determine the full path to the libasan.so library
        #
    endif()
endif()

## Website front page is done manually here for now...
## (At least until Snap! Websites works as expected for us.)

execute_process(
    COMMAND "date" "+%Y.%m.%d"
    OUTPUT_VARIABLE NOW
)

# Win32 date would use something like this
# execute_process(COMMAND "cmd" " /C date /T" OUTPUT_VARIABLE ${RESULT})

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/front-page.html.in
    ${CMAKE_CURRENT_BINARY_DIR}/front-page.html
)

# vim: ts=4 sw=4 et nocindent
