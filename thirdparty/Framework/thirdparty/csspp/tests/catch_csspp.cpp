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
 * \brief Test the csspp.cpp file.
 *
 * This test runs a battery of tests agains the csspp.cpp
 * implementation to ensure full coverage.
 */

// css
//
#include    <csspp/lexer.h>

#include    <csspp/exception.h>
#include    <csspp/unicode_range.h>


// self
//
#include    "catch_main.h"


// C++
//
#include    <sstream>


// C lib
//
#include    <string.h>


// last include
//
#include    <snapdev/poison.h>



namespace
{


} // no name namespace


CATCH_TEST_CASE("Version string", "[csspp] [version]")
{
    // we expect the test suite to be compiled with the exact same version
    CATCH_REQUIRE(csspp::csspp_library_version() == std::string(CSSPP_VERSION));

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Safe boolean", "[csspp] [output]")
{
    {
        bool flag(false);
        CATCH_REQUIRE(flag == false);
        {
            csspp::safe_bool_t safe(flag);
            CATCH_REQUIRE(flag == true);
        }
        CATCH_REQUIRE(flag == false);
    }

    {
        bool flag(false);
        CATCH_REQUIRE(flag == false);
        {
            csspp::safe_bool_t safe(flag);
            CATCH_REQUIRE(flag == true);
            flag = false;
            CATCH_REQUIRE(flag == false);
        }
        CATCH_REQUIRE(flag == false);
    }

    {
        bool flag(true);
        CATCH_REQUIRE(flag == true);
        {
            csspp::safe_bool_t safe(flag);
            CATCH_REQUIRE(flag == true);
            flag = false;
            CATCH_REQUIRE(flag == false);
        }
        CATCH_REQUIRE(flag == true);
    }

    {
        bool flag(false);
        CATCH_REQUIRE(flag == false);
        {
            csspp::safe_bool_t safe(flag, true);
            CATCH_REQUIRE(flag == true);
            flag = false;
            CATCH_REQUIRE(flag == false);
        }
        CATCH_REQUIRE(flag == false);
    }

    {
        bool flag(false);
        CATCH_REQUIRE(flag == false);
        {
            csspp::safe_bool_t safe(flag, false);
            CATCH_REQUIRE(flag == false);
            flag = true;
            CATCH_REQUIRE(flag == true);
        }
        CATCH_REQUIRE(flag == false);
    }

    {
        bool flag(true);
        CATCH_REQUIRE(flag == true);
        {
            csspp::safe_bool_t safe(flag, true);
            CATCH_REQUIRE(flag == true);
            flag = false;
            CATCH_REQUIRE(flag == false);
        }
        CATCH_REQUIRE(flag == true);
    }

    {
        bool flag(true);
        CATCH_REQUIRE(flag == true);
        {
            csspp::safe_bool_t safe(flag, false);
            CATCH_REQUIRE(flag == false);
            flag = true;
            CATCH_REQUIRE(flag == true);
        }
        CATCH_REQUIRE(flag == true);
    }
}

CATCH_TEST_CASE("Decimal number output", "[csspp] [output]")
{
    CATCH_REQUIRE(csspp::decimal_number_to_string(1.0, false) == "1");
    CATCH_REQUIRE(csspp::decimal_number_to_string(1.2521, false) == "1.252");
    CATCH_REQUIRE(csspp::decimal_number_to_string(1.2526, false) == "1.253");
    CATCH_REQUIRE(csspp::decimal_number_to_string(0.0, false) == "0");
    CATCH_REQUIRE(csspp::decimal_number_to_string(0.2521, false) == "0.252");
    CATCH_REQUIRE(csspp::decimal_number_to_string(0.2526, false) == "0.253");
    {
        csspp::safe_precision_t precision(2);
        CATCH_REQUIRE(csspp::decimal_number_to_string(1.2513, false) == "1.25");
        CATCH_REQUIRE(csspp::decimal_number_to_string(1.2561, false) == "1.26");
    }
    CATCH_REQUIRE(csspp::decimal_number_to_string(-1.2526, false) == "-1.253");
    CATCH_REQUIRE(csspp::decimal_number_to_string(-0.9, false) == "-0.9");
    CATCH_REQUIRE(csspp::decimal_number_to_string(-0.0009, false) == "-0.001");
    CATCH_REQUIRE(csspp::decimal_number_to_string(-1000.0, false) == "-1000");
    CATCH_REQUIRE(csspp::decimal_number_to_string(1000.0, false) == "1000");
    CATCH_REQUIRE(csspp::decimal_number_to_string(100.0, false) == "100");
    CATCH_REQUIRE(csspp::decimal_number_to_string(10.0, false) == "10");

    CATCH_REQUIRE(csspp::decimal_number_to_string(1.0, true) == "1");
    CATCH_REQUIRE(csspp::decimal_number_to_string(1.2521, true) == "1.252");
    CATCH_REQUIRE(csspp::decimal_number_to_string(1.2526, true) == "1.253");
    CATCH_REQUIRE(csspp::decimal_number_to_string(0.0, true) == "0");
    CATCH_REQUIRE(csspp::decimal_number_to_string(0.2521, true) == ".252");
    CATCH_REQUIRE(csspp::decimal_number_to_string(0.2526, true) == ".253");
    {
        csspp::safe_precision_t precision(2);
        CATCH_REQUIRE(csspp::decimal_number_to_string(1.2513, true) == "1.25");
        CATCH_REQUIRE(csspp::decimal_number_to_string(1.2561, true) == "1.26");
    }
    CATCH_REQUIRE(csspp::decimal_number_to_string(-1.2526, true) == "-1.253");
    CATCH_REQUIRE(csspp::decimal_number_to_string(-0.9, true) == "-.9");
    CATCH_REQUIRE(csspp::decimal_number_to_string(-0.0009, true) == "-.001");
    CATCH_REQUIRE(csspp::decimal_number_to_string(-1000.0, true) == "-1000");
    CATCH_REQUIRE(csspp::decimal_number_to_string(1000.0, true) == "1000");
    CATCH_REQUIRE(csspp::decimal_number_to_string(100.0, true) == "100");
    CATCH_REQUIRE(csspp::decimal_number_to_string(10.0, true) == "10");

    // super small negative numbers must be output as "0"
    CATCH_REQUIRE(csspp::decimal_number_to_string(-1.2526e-10, true) == "0");
}

CATCH_TEST_CASE("Invalid precision", "[csspp] [invalid]")
{
    // we want to keep the default precision in place
    csspp::safe_precision_t precision;

    // negative not available
    for(int i(-10); i < 0; ++i)
    {
        CATCH_REQUIRE_THROWS_AS(csspp::set_precision(i), csspp::csspp_exception_overflow);
    }

    // too large not acceptable
    for(int i(11); i <= 20; ++i)
    {
        CATCH_REQUIRE_THROWS_AS(csspp::set_precision(i), csspp::csspp_exception_overflow);
    }
}

// vim: ts=4 sw=4 et
