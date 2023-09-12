# Copyright (c) 2015-2022  Made to Order Software Corp.  All Rights Reserved
#
# https://snapwebsites.org/project/csspp
# contact@m2osw.com
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License along
# with this library; if not, write to the Free Software Foundation, Inc.,
# 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA

##
## CSS Pre-Processor Unit Tests
##
project(unittest)

find_package(SnapCatch2)

if(SnapCatch2_FOUND)

    add_executable(${PROJECT_NAME}
        catch_main.cpp

        catch_assembler.cpp
        catch_color.cpp
        catch_compiler.cpp
        catch_csspp.cpp
        catch_error.cpp
        catch_expr_additive.cpp
        catch_expr_conditional.cpp
        catch_expr_equality.cpp
        catch_expr_list.cpp
        catch_expr_logical_and.cpp
        catch_expr_logical_or.cpp
        catch_expr_multiplicative.cpp
        catch_expr_power.cpp
        catch_expr_relational.cpp
        catch_expr_unary.cpp
        catch_internal_functions.cpp
        catch_lexer.cpp
        catch_node.cpp
        catch_nth_child.cpp
        catch_parser.cpp
        catch_position.cpp
        catch_unicode_range.cpp
    )

    target_include_directories(${PROJECT_NAME}
        PUBLIC
            ${SNAPDEV_INCLUDE_DIRS}
            ${LIBEXCEPT_INCLUDE_DIRS}
            ${SNAPCATCH2_INCLUDE_DIRS}
    )

    target_link_libraries(${PROJECT_NAME}
        csspp
        ${LIBEXCEPT_LIBRARIES}
        ${SNAPCATCH2_LIBRARIES}
    )

    # You can use the --success command line option to see all the tests
    # as they run; it is a LOT of output though, thus by default we don't
    # use it
    add_test(unittest ${PROJECT_NAME})

else(SnapCatch2_FOUND)

    message("snapcatch2 not found... no test will be built.")

endif(SnapCatch2_FOUND)

# vim: ts=4 sw=4 et
