// Copyright (c) 2015-2022  Made to Order Software Corp.  All Rights Reserved
//
// https://snapwebsites.org/project/csspp
// contact@m2osw.com
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

/** \file
 * \brief Test the error.cpp file.
 *
 * This test runs a battery of tests agains the error.cpp
 * implementation to ensure full coverage.
 */

// csspp
//
#include    <csspp/error.h>
#include    <csspp/exception.h>
#include    <csspp/lexer.h>
#include    <csspp/unicode_range.h>


// self
//
#include    "catch_main.h"


// C++
//
#include    <sstream>


// C
//
#include    <string.h>


// last include
//
#include    <snapdev/poison.h>



namespace
{


} // no name namespace


CATCH_TEST_CASE("Position defaults", "[position] [defaults]")
{
    {
        csspp::position pos("pos.css");

        CATCH_REQUIRE(pos.get_filename() == "pos.css");
        CATCH_REQUIRE(pos.get_line() == 1);
        CATCH_REQUIRE(pos.get_page() == 1);
        CATCH_REQUIRE(pos.get_total_line() == 1);

        csspp::position other("other.css");

        CATCH_REQUIRE(other.get_filename() == "other.css");
        CATCH_REQUIRE(other.get_line() == 1);
        CATCH_REQUIRE(other.get_page() == 1);
        CATCH_REQUIRE(other.get_total_line() == 1);

        // copy works as expected?
        other = pos;

        CATCH_REQUIRE(pos.get_filename() == "pos.css");
        CATCH_REQUIRE(pos.get_line() == 1);
        CATCH_REQUIRE(pos.get_page() == 1);
        CATCH_REQUIRE(pos.get_total_line() == 1);

        CATCH_REQUIRE(other.get_filename() == "pos.css");
        CATCH_REQUIRE(other.get_line() == 1);
        CATCH_REQUIRE(other.get_page() == 1);
        CATCH_REQUIRE(other.get_total_line() == 1);
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Position counters", "[position] [count]")
{
    // simple check to verify there is no interaction between
    // a copy and the original
    {
        csspp::position pos("pos.css");

        CATCH_REQUIRE(pos.get_filename() == "pos.css");
        CATCH_REQUIRE(pos.get_line() == 1);
        CATCH_REQUIRE(pos.get_page() == 1);
        CATCH_REQUIRE(pos.get_total_line() == 1);

        csspp::position other("other.css");

        CATCH_REQUIRE(other.get_filename() == "other.css");
        CATCH_REQUIRE(other.get_line() == 1);
        CATCH_REQUIRE(other.get_page() == 1);
        CATCH_REQUIRE(other.get_total_line() == 1);

        // copy works as expected?
        other = pos;

        CATCH_REQUIRE(pos.get_filename() == "pos.css");
        CATCH_REQUIRE(pos.get_line() == 1);
        CATCH_REQUIRE(pos.get_page() == 1);
        CATCH_REQUIRE(pos.get_total_line() == 1);

        CATCH_REQUIRE(other.get_filename() == "pos.css"); // filename changed!
        CATCH_REQUIRE(other.get_line() == 1);
        CATCH_REQUIRE(other.get_page() == 1);
        CATCH_REQUIRE(other.get_total_line() == 1);

        // increment does not affect another position
        other.next_line();

        CATCH_REQUIRE(pos.get_filename() == "pos.css");
        CATCH_REQUIRE(pos.get_line() == 1);
        CATCH_REQUIRE(pos.get_page() == 1);
        CATCH_REQUIRE(pos.get_total_line() == 1);

        CATCH_REQUIRE(other.get_filename() == "pos.css");
        CATCH_REQUIRE(other.get_line() == 2);
        CATCH_REQUIRE(other.get_page() == 1);
        CATCH_REQUIRE(other.get_total_line() == 2);

        // increment does not affect another position
        other.next_page();

        CATCH_REQUIRE(pos.get_filename() == "pos.css");
        CATCH_REQUIRE(pos.get_line() == 1);
        CATCH_REQUIRE(pos.get_page() == 1);
        CATCH_REQUIRE(pos.get_total_line() == 1);

        CATCH_REQUIRE(other.get_filename() == "pos.css");
        CATCH_REQUIRE(other.get_line() == 1);
        CATCH_REQUIRE(other.get_page() == 2);
        CATCH_REQUIRE(other.get_total_line() == 2);
    }

    // loop and increment line/page counters
    {
        csspp::position pos("counters.css");
        int line(1);
        int page(1);
        int total_line(1);

        for(int i(0); i < 1000; ++i)
        {
            if(rand() & 1)
            {
                pos.next_line();
                ++line;
                ++total_line;
            }
            else
            {
                pos.next_page();
                line = 1;
                ++page;
                //++total_line; -- should this happen?
            }

            CATCH_REQUIRE(pos.get_filename() == "counters.css");
            CATCH_REQUIRE(pos.get_line() == line);
            CATCH_REQUIRE(pos.get_page() == page);
            CATCH_REQUIRE(pos.get_total_line() == total_line);
        }
    }

    // no error left over
    VERIFY_ERRORS("");
}

// vim: ts=4 sw=4 et
