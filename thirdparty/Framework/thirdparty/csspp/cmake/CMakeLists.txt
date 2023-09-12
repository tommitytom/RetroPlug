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

project(csspp)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/CSSPPConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/CSSPPConfig.cmake
    @ONLY
)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/CSSPPConfigVersion.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/CSSPPConfigVersion.cmake
    @ONLY
)

install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/CSSPPConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/CSSPPConfigVersion.cmake

    DESTINATION
        share/cmake/CSSPP
)

# Local Variables:
# indent-tabs-mode: nil
# tab-width: 4
# End:

# vim: ts=4 sw=4 et nocindent
