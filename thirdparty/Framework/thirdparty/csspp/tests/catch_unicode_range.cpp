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
 * \brief Test the unicode_range.cpp file.
 *
 * This test runs a battery of tests agains the unicode_range.cpp
 * implementation to ensure full coverage.
 */

// csspp
//
#include    <csspp/unicode_range.h>

#include    <csspp/exception.h>
#include    <csspp/lexer.h>


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


CATCH_TEST_CASE("Unicode range: start/end equal", "[unicode-range] [code-point]")
{
    for(int i(0); i < 1000; ++i)
    {
        csspp::wide_char_t unicode(rand() % 0x110000);
        csspp::unicode_range_t range(unicode, unicode);

        std::stringstream ss;
        ss << std::hex << unicode;

        CATCH_REQUIRE(range.to_string() == ss.str());
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Unicode range: start/end differ", "[unicode-range] [range]")
{
    for(int i(1); i < 15; ++i)
    {
        csspp::unicode_range_t range(0, i);

        std::stringstream ss;
        ss << std::hex << "0-" << i;

        CATCH_REQUIRE(range.to_string() == ss.str());
    }

    for(int i(0); i < 1000; ++i)
    {
        csspp::wide_char_t unicode_start;
        csspp::wide_char_t unicode_end;

        // try once with the range construtor
        do
        {
            unicode_start = rand() % 0x110000;
            unicode_end = rand() % 0x110000;
            if(unicode_start > unicode_end)
            {
                std::swap(unicode_start, unicode_end);
            }
            while((unicode_end & 15) == 15)
            {
                unicode_end &= rand() % 15;
                if(unicode_start > unicode_end)
                {
                    std::swap(unicode_start, unicode_end);
                }
            }
        }
        while(unicode_start == unicode_end);
        csspp::unicode_range_t range(unicode_start, unicode_end);

        csspp_test::our_unicode_range_t our_range(unicode_start, unicode_end);
        CATCH_REQUIRE(range.get_range() == our_range.get_range());

        CATCH_REQUIRE(range.get_start() == unicode_start);
        CATCH_REQUIRE(range.get_end() == unicode_end);

        std::stringstream ss;
        ss << std::hex << unicode_start << "-" << unicode_end;

        CATCH_REQUIRE(range.to_string() == ss.str());

        // try again with new values and using set_range(start, end)
        do
        {
            unicode_start = rand() % 0x110000;
            unicode_end = rand() % 0x110000;
            if(unicode_start > unicode_end)
            {
                std::swap(unicode_start, unicode_end);
            }
            while((unicode_end & 15) == 15)
            {
                unicode_end &= rand() % 15;
                if(unicode_start > unicode_end)
                {
                    std::swap(unicode_start, unicode_end);
                }
            }
        }
        while(unicode_start == unicode_end);
        range.set_range(unicode_start, unicode_end);

        our_range.set_start(unicode_start);
        our_range.set_end(unicode_end);
        CATCH_REQUIRE(range.get_range() == our_range.get_range());

        CATCH_REQUIRE(range.get_start() == unicode_start);
        CATCH_REQUIRE(range.get_end() == unicode_end);

        ss.str("");
        ss << std::hex << unicode_start << "-" << unicode_end;

        CATCH_REQUIRE(range.to_string() == ss.str());

        // try again with new values and using set_range(range)
        do
        {
            unicode_start = rand() % 0x110000;
            unicode_end = rand() % 0x110000;
            if(unicode_start > unicode_end)
            {
                std::swap(unicode_start, unicode_end);
            }
            while((unicode_end & 15) == 15)
            {
                unicode_end &= rand() % 15;
                if(unicode_start > unicode_end)
                {
                    std::swap(unicode_start, unicode_end);
                }
            }
        }
        while(unicode_start == unicode_end);

        our_range.set_start(unicode_start);
        our_range.set_end(unicode_end);

        range.set_range(our_range.get_range());

        CATCH_REQUIRE(range.get_range() == our_range.get_range());

        CATCH_REQUIRE(range.get_start() == unicode_start);
        CATCH_REQUIRE(range.get_end() == unicode_end);

        ss.str("");
        ss << std::hex << unicode_start << "-" << unicode_end;

        CATCH_REQUIRE(range.to_string() == ss.str());

        // try again with new values and using range constructor with range value
        do
        {
            unicode_start = rand() % 0x110000;
            unicode_end = rand() % 0x110000;
            if(unicode_start > unicode_end)
            {
                std::swap(unicode_start, unicode_end);
            }
            while((unicode_end & 15) == 15)
            {
                unicode_end &= rand() % 15;
                if(unicode_start > unicode_end)
                {
                    std::swap(unicode_start, unicode_end);
                }
            }
        }
        while(unicode_start == unicode_end);

        our_range.set_start(unicode_start);
        our_range.set_end(unicode_end);

        csspp::unicode_range_t new_range(our_range.get_range());

        CATCH_REQUIRE(new_range.get_range() == our_range.get_range());

        CATCH_REQUIRE(new_range.get_start() == unicode_start);
        CATCH_REQUIRE(new_range.get_end() == unicode_end);

        ss.str("");
        ss << std::hex << unicode_start << "-" << unicode_end;

        CATCH_REQUIRE(new_range.to_string() == ss.str());
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Unicode range: 6 x '?'", "[unicode-range] [mask]")
{
    // the mask of 6 x '?' is a special case
    {
        csspp::unicode_range_t range(0, 0x10FFFF);

        CATCH_REQUIRE(range.to_string() == "??????");
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Unicode range: 5 x '?'", "[unicode-range] [mask]")
{
    // a mask of 5 x '?' has a very few possibilities
    {
        csspp::unicode_range_t range(0, 0x0FFFFF);

        CATCH_REQUIRE(range.to_string() == "?????");
    }

    {
        // this is a special range where the maximum (end) is larger
        // than what Unicode otherwise allows...
        csspp::unicode_range_t range(0x100000, 0x1FFFFF);

        CATCH_REQUIRE(range.to_string() == "1?????");
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Unicode range: 4 x '?'", "[unicode-range] [mask]")
{
    // a mask of 4 x '?' has a small number of possibilities
    {
        csspp::unicode_range_t range(0, 0x00FFFF);

        CATCH_REQUIRE(range.to_string() == "????");
    }

    for(int i(1); i < 0x11; ++i)
    {
        csspp::unicode_range_t range(i << 16, (i << 16) | 0x00FFFF);

        std::stringstream ss;
        ss << std::hex << i << "????";

        CATCH_REQUIRE(range.to_string() == ss.str());
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Unicode range: 3 x '?'", "[unicode-range] [mask]")
{
    // a mask of 3 x '?' has a small number of possibilities
    {
        csspp::unicode_range_t range(0, 0x000FFF);

        CATCH_REQUIRE(range.to_string() == "???");
    }

    for(int i(1); i < 0x10F; ++i)
    {
        csspp::unicode_range_t range(i << 12, (i << 12) | 0x000FFF);

        std::stringstream ss;
        ss << std::hex << i << "???";

        CATCH_REQUIRE(range.to_string() == ss.str());
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Unicode range: 2 x '?'", "[unicode-range] [mask]")
{
    // a mask of 2 x '?' has a small number of possibilities
    {
        csspp::unicode_range_t range(0, 0x0000FF);

        CATCH_REQUIRE(range.to_string() == "??");
    }

    for(int i(1); i < 0x10FF; ++i)
    {
        csspp::unicode_range_t range(i << 8, (i << 8) | 0x0000FF);

        std::stringstream ss;
        ss << std::hex << i << "??";

        CATCH_REQUIRE(range.to_string() == ss.str());
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Unicode range: 1 x '?'", "[unicode-range] [mask]")
{
    // a mask of 1 x '?' has a small number of possibilities
    {
        csspp::unicode_range_t range(0, 0x00000F);

        CATCH_REQUIRE(range.to_string() == "?");
    }

    for(int i(1); i < 0x10FFF; ++i)
    {
        csspp::unicode_range_t range(i << 4, (i << 4) | 0x00000F);

        std::stringstream ss;
        ss << std::hex << i << "?";

        CATCH_REQUIRE(range.to_string() == ss.str());
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Unicode range: invalid start/end values", "[unicode-range] [code-point]")
{
    // Do a little more?
    CATCH_REQUIRE_THROWS_AS(new csspp::unicode_range_t(0x110000, 0x012345), csspp::csspp_exception_overflow);
    CATCH_REQUIRE_THROWS_AS(new csspp::unicode_range_t(0x012345, 0x200000), csspp::csspp_exception_overflow);
    CATCH_REQUIRE_THROWS_AS(new csspp::unicode_range_t(0x004000, 0x000200), csspp::csspp_exception_overflow);

    // no error left over
    VERIFY_ERRORS("");
}

// vim: ts=4 sw=4 et
