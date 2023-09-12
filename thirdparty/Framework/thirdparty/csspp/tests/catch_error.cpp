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



CATCH_TEST_CASE("Error names", "[error]")
{
    csspp::error_mode_t e(csspp::error_mode_t::ERROR_DEC);
    while(e <= csspp::error_mode_t::ERROR_WARNING)
    {
        std::stringstream ss;
        ss << e;
        std::string const name(ss.str());

        switch(e)
        {
        case csspp::error_mode_t::ERROR_DEBUG:
            CATCH_REQUIRE(name == "debug");
            break;

        case csspp::error_mode_t::ERROR_DEC:
            CATCH_REQUIRE(name == "dec");
            break;

        case csspp::error_mode_t::ERROR_ERROR:
            CATCH_REQUIRE(name == "error");
            break;

        case csspp::error_mode_t::ERROR_FATAL:
            CATCH_REQUIRE(name == "fatal");
            break;

        case csspp::error_mode_t::ERROR_HEX:
            CATCH_REQUIRE(name == "hex");
            break;

        case csspp::error_mode_t::ERROR_INFO:
            CATCH_REQUIRE(name == "info");
            break;

        case csspp::error_mode_t::ERROR_WARNING:
            CATCH_REQUIRE(name == "warning");
            break;

        }

        e = static_cast<csspp::error_mode_t>(static_cast<int>(e) + 1);
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Error messages", "[error] [output]")
{
    csspp::error_count_t error_count(csspp::error::instance().get_error_count());
    csspp::error_count_t warning_count(csspp::error::instance().get_warning_count());

    csspp::position p("test.css");

    {
        csspp::error_happened_t happened;

        csspp::error::instance() << p << "testing errors: "
                                 << 123
                                 << " U+" << csspp::error_mode_t::ERROR_HEX << 123
                                 << "."
                                 << csspp::error_mode_t::ERROR_FATAL;
        VERIFY_ERRORS("test.css(1): fatal: testing errors: 123 U+7b.\n");
        ++error_count;
        CATCH_REQUIRE(error_count == csspp::error::instance().get_error_count());
        CATCH_REQUIRE(warning_count == csspp::error::instance().get_warning_count());

        CATCH_REQUIRE(happened.error_happened());
        CATCH_REQUIRE_FALSE(happened.warning_happened());
    }

    {
        csspp::error_happened_t happened;

        int64_t cs(83);
        csspp::error::instance() << p << std::string("testing errors:")
                                 << " U+" << csspp::error_mode_t::ERROR_HEX << cs
                                 << " (" << csspp::error_mode_t::ERROR_DEC << 133
                                 << ")."
                                 << csspp::error_mode_t::ERROR_ERROR;
        VERIFY_ERRORS("test.css(1): error: testing errors: U+53 (133).\n");
        ++error_count;
        CATCH_REQUIRE(error_count == csspp::error::instance().get_error_count());
        CATCH_REQUIRE(warning_count == csspp::error::instance().get_warning_count());

        CATCH_REQUIRE(happened.error_happened());
        CATCH_REQUIRE_FALSE(happened.warning_happened());
    }

    {
        csspp::safe_error_t safe_error;

        {
            csspp::error_happened_t happened;

            csspp::error::instance() << p << "testing warnings:"
                                     << " U+" << csspp::error_mode_t::ERROR_HEX << 123
                                     << " decimal: " << csspp::error_mode_t::ERROR_DEC << 123.25
                                     << "."
                                     << csspp::error_mode_t::ERROR_WARNING;
            VERIFY_ERRORS("test.css(1): warning: testing warnings: U+7b decimal: 123.25.\n");
            ++warning_count;
            CATCH_REQUIRE(error_count == csspp::error::instance().get_error_count());
            CATCH_REQUIRE(warning_count == csspp::error::instance().get_warning_count());

            CATCH_REQUIRE_FALSE(happened.error_happened());
            CATCH_REQUIRE(happened.warning_happened());
        }

        {
            csspp::error_happened_t happened;

            csspp::error::instance().set_count_warnings_as_errors(true);
            csspp::error::instance() << p << "testing warnings:"
                                     << " U+" << csspp::error_mode_t::ERROR_HEX << 123
                                     << " decimal: " << csspp::error_mode_t::ERROR_DEC << 123.25
                                     << "."
                                     << csspp::error_mode_t::ERROR_WARNING;
            VERIFY_ERRORS("test.css(1): warning: testing warnings: U+7b decimal: 123.25.\n");
            ++error_count;
            CATCH_REQUIRE(error_count == csspp::error::instance().get_error_count());
            CATCH_REQUIRE(warning_count == csspp::error::instance().get_warning_count());
            csspp::error::instance().set_count_warnings_as_errors(false);

            CATCH_REQUIRE(happened.error_happened());
            CATCH_REQUIRE_FALSE(happened.warning_happened());
        }

        {
            csspp::error_happened_t happened;

            csspp::error::instance() << p << "testing warnings:"
                                     << " U+" << csspp::error_mode_t::ERROR_HEX << 123
                                     << " decimal: " << csspp::error_mode_t::ERROR_DEC << 123.25
                                     << "."
                                     << csspp::error_mode_t::ERROR_WARNING;
            VERIFY_ERRORS("test.css(1): warning: testing warnings: U+7b decimal: 123.25.\n");
            ++warning_count;
            CATCH_REQUIRE(error_count == csspp::error::instance().get_error_count());
            CATCH_REQUIRE(warning_count == csspp::error::instance().get_warning_count());

            CATCH_REQUIRE_FALSE(happened.error_happened());
            CATCH_REQUIRE(happened.warning_happened());
        }
    }
    // the safe_error restores the counters to what they were before the '{'
    --error_count;
    warning_count -= 2;
    CATCH_REQUIRE(error_count == csspp::error::instance().get_error_count());
    CATCH_REQUIRE(warning_count == csspp::error::instance().get_warning_count());

    {
        csspp::error_happened_t happened;

        csspp::error::instance() << p << "testing info:"
                                 << " U+" << csspp::error_mode_t::ERROR_HEX << 120
                                 << " decimal: " << csspp::error_mode_t::ERROR_DEC << 213.25
                                 << "."
                                 << csspp::error_mode_t::ERROR_INFO;
        VERIFY_ERRORS("test.css(1): info: testing info: U+78 decimal: 213.25.\n");
        CATCH_REQUIRE(error_count == csspp::error::instance().get_error_count());
        CATCH_REQUIRE(warning_count == csspp::error::instance().get_warning_count());

        CATCH_REQUIRE_FALSE(happened.error_happened());
        CATCH_REQUIRE_FALSE(happened.warning_happened());
    }

    {
        csspp::error_happened_t happened;

        csspp::error::instance() << p << "testing debug:"
                                 << " U+" << csspp::error_mode_t::ERROR_HEX << 112
                                 << " decimal: " << csspp::error_mode_t::ERROR_DEC << 13.25
                                 << "."
                                 << csspp::error_mode_t::ERROR_DEBUG;
        VERIFY_ERRORS("");
        CATCH_REQUIRE(error_count == csspp::error::instance().get_error_count());
        CATCH_REQUIRE(warning_count == csspp::error::instance().get_warning_count());

        CATCH_REQUIRE_FALSE(happened.error_happened());
        CATCH_REQUIRE_FALSE(happened.warning_happened());
    }

    {
        csspp::error_happened_t happened;

        csspp::error::instance().set_show_debug(true);
        csspp::error::instance() << p << "testing debug:"
                                 << " U+" << csspp::error_mode_t::ERROR_HEX << 112
                                 << " decimal: " << csspp::error_mode_t::ERROR_DEC << 13.25
                                 << "."
                                 << csspp::error_mode_t::ERROR_DEBUG;
        VERIFY_ERRORS("test.css(1): debug: testing debug: U+70 decimal: 13.25.\n");
        CATCH_REQUIRE(error_count == csspp::error::instance().get_error_count());
        CATCH_REQUIRE(warning_count == csspp::error::instance().get_warning_count());
        csspp::error::instance().set_show_debug(false);

        CATCH_REQUIRE_FALSE(happened.error_happened());
        CATCH_REQUIRE_FALSE(happened.warning_happened());
    }

    {
        csspp::error_happened_t happened;

        csspp::error::instance() << p << "testing debug:"
                                 << " U+" << csspp::error_mode_t::ERROR_HEX << 112
                                 << " decimal: " << csspp::error_mode_t::ERROR_DEC << 13.25
                                 << "."
                                 << csspp::error_mode_t::ERROR_DEBUG;
        VERIFY_ERRORS("");
        CATCH_REQUIRE(error_count == csspp::error::instance().get_error_count());
        CATCH_REQUIRE(warning_count == csspp::error::instance().get_warning_count());

        CATCH_REQUIRE_FALSE(happened.error_happened());
        CATCH_REQUIRE_FALSE(happened.warning_happened());
    }

    {
        csspp::error_happened_t happened;

        csspp::error::instance().set_verbose(true);
        csspp::error::instance() << p << "verbose message to debug the compiler."
                                 << csspp::error_mode_t::ERROR_INFO;
        VERIFY_ERRORS("test.css(1): info: verbose message to debug the compiler.\n");
        CATCH_REQUIRE(error_count == csspp::error::instance().get_error_count());
        CATCH_REQUIRE(warning_count == csspp::error::instance().get_warning_count());

        CATCH_REQUIRE_FALSE(happened.error_happened());
        CATCH_REQUIRE_FALSE(happened.warning_happened());

        csspp::error::instance().set_verbose(false);
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Error messages when hidden", "[error] [output] [hidden]")
{
    csspp::error_count_t error_count(csspp::error::instance().get_error_count());
    csspp::error_count_t warning_count(csspp::error::instance().get_warning_count());

    csspp::error::instance().set_hide_all(true);

    csspp::position p("test.css");

    {
        csspp::error_happened_t happened;

        csspp::error::instance() << p << "testing errors: "
                                 << 123
                                 << " U+" << csspp::error_mode_t::ERROR_HEX << 123
                                 << "."
                                 << csspp::error_mode_t::ERROR_FATAL;
        VERIFY_ERRORS("test.css(1): fatal: testing errors: 123 U+7b.\n");
        ++error_count;
        CATCH_REQUIRE(error_count == csspp::error::instance().get_error_count());
        CATCH_REQUIRE(warning_count == csspp::error::instance().get_warning_count());

        CATCH_REQUIRE(happened.error_happened());
        CATCH_REQUIRE_FALSE(happened.warning_happened());
    }

    {
        csspp::error_happened_t happened;

        int64_t cs(83);
        csspp::error::instance() << p << std::string("testing errors:")
                                 << " U+" << csspp::error_mode_t::ERROR_HEX << cs
                                 << " (" << csspp::error_mode_t::ERROR_DEC << 133
                                 << ")."
                                 << csspp::error_mode_t::ERROR_ERROR;
        VERIFY_ERRORS("test.css(1): error: testing errors: U+53 (133).\n");
        ++error_count;
        CATCH_REQUIRE(error_count == csspp::error::instance().get_error_count());
        CATCH_REQUIRE(warning_count == csspp::error::instance().get_warning_count());

        CATCH_REQUIRE(happened.error_happened());
        CATCH_REQUIRE_FALSE(happened.warning_happened());
    }

    {
        csspp::safe_error_t safe_error;

        {
            csspp::error_happened_t happened;

            csspp::error::instance() << p << "testing warnings:"
                                     << " U+" << csspp::error_mode_t::ERROR_HEX << 123
                                     << " decimal: " << csspp::error_mode_t::ERROR_DEC << 123.25
                                     << "."
                                     << csspp::error_mode_t::ERROR_WARNING;
            VERIFY_ERRORS("");
            CATCH_REQUIRE(error_count == csspp::error::instance().get_error_count());
            CATCH_REQUIRE(warning_count == csspp::error::instance().get_warning_count());

            CATCH_REQUIRE_FALSE(happened.error_happened());
            CATCH_REQUIRE_FALSE(happened.warning_happened());
        }

        {
            csspp::error_happened_t happened;

            csspp::error::instance().set_count_warnings_as_errors(true);
            csspp::error::instance() << p << "testing warnings:"
                                     << " U+" << csspp::error_mode_t::ERROR_HEX << 123
                                     << " decimal: " << csspp::error_mode_t::ERROR_DEC << 123.25
                                     << "."
                                     << csspp::error_mode_t::ERROR_WARNING;
            VERIFY_ERRORS("test.css(1): warning: testing warnings: U+7b decimal: 123.25.\n");
            ++error_count;
            CATCH_REQUIRE(error_count == csspp::error::instance().get_error_count());
            CATCH_REQUIRE(warning_count == csspp::error::instance().get_warning_count());
            csspp::error::instance().set_count_warnings_as_errors(false);

            CATCH_REQUIRE(happened.error_happened());
            CATCH_REQUIRE_FALSE(happened.warning_happened());
        }

        {
            csspp::error_happened_t happened;

            csspp::error::instance() << p << "testing warnings:"
                                     << " U+" << csspp::error_mode_t::ERROR_HEX << 123
                                     << " decimal: " << csspp::error_mode_t::ERROR_DEC << 123.25
                                     << "."
                                     << csspp::error_mode_t::ERROR_WARNING;
            VERIFY_ERRORS("");
            CATCH_REQUIRE(error_count == csspp::error::instance().get_error_count());
            CATCH_REQUIRE(warning_count == csspp::error::instance().get_warning_count());

            CATCH_REQUIRE_FALSE(happened.error_happened());
            CATCH_REQUIRE_FALSE(happened.warning_happened());
        }
    }
    // the safe_error restores the counters to what they were before the '{'
    --error_count;
    warning_count -= 0;
    CATCH_REQUIRE(error_count == csspp::error::instance().get_error_count());
    CATCH_REQUIRE(warning_count == csspp::error::instance().get_warning_count());

    {
        csspp::error_happened_t happened;

        csspp::error::instance() << p << "testing info:"
                                 << " U+" << csspp::error_mode_t::ERROR_HEX << 120
                                 << " decimal: " << csspp::error_mode_t::ERROR_DEC << 213.25
                                 << "."
                                 << csspp::error_mode_t::ERROR_INFO;
        VERIFY_ERRORS("");
        CATCH_REQUIRE(error_count == csspp::error::instance().get_error_count());
        CATCH_REQUIRE(warning_count == csspp::error::instance().get_warning_count());

        CATCH_REQUIRE_FALSE(happened.error_happened());
        CATCH_REQUIRE_FALSE(happened.warning_happened());
    }

    {
        csspp::error_happened_t happened;

        csspp::error::instance() << p << "testing debug:"
                                 << " U+" << csspp::error_mode_t::ERROR_HEX << 112
                                 << " decimal: " << csspp::error_mode_t::ERROR_DEC << 13.25
                                 << "."
                                 << csspp::error_mode_t::ERROR_DEBUG;
        VERIFY_ERRORS("");
        CATCH_REQUIRE(error_count == csspp::error::instance().get_error_count());
        CATCH_REQUIRE(warning_count == csspp::error::instance().get_warning_count());

        CATCH_REQUIRE_FALSE(happened.error_happened());
        CATCH_REQUIRE_FALSE(happened.warning_happened());
    }

    {
        csspp::error_happened_t happened;

        csspp::error::instance().set_show_debug(true);
        csspp::error::instance() << p << "testing debug:"
                                 << " U+" << csspp::error_mode_t::ERROR_HEX << 112
                                 << " decimal: " << csspp::error_mode_t::ERROR_DEC << 13.25
                                 << "."
                                 << csspp::error_mode_t::ERROR_DEBUG;
        VERIFY_ERRORS("");
        CATCH_REQUIRE(error_count == csspp::error::instance().get_error_count());
        CATCH_REQUIRE(warning_count == csspp::error::instance().get_warning_count());
        csspp::error::instance().set_show_debug(false);

        CATCH_REQUIRE_FALSE(happened.error_happened());
        CATCH_REQUIRE_FALSE(happened.warning_happened());
    }

    {
        csspp::error_happened_t happened;

        csspp::error::instance() << p << "testing debug:"
                                 << " U+" << csspp::error_mode_t::ERROR_HEX << 112
                                 << " decimal: " << csspp::error_mode_t::ERROR_DEC << 13.25
                                 << "."
                                 << csspp::error_mode_t::ERROR_DEBUG;
        VERIFY_ERRORS("");
        CATCH_REQUIRE(error_count == csspp::error::instance().get_error_count());
        CATCH_REQUIRE(warning_count == csspp::error::instance().get_warning_count());

        CATCH_REQUIRE_FALSE(happened.error_happened());
        CATCH_REQUIRE_FALSE(happened.warning_happened());
    }

    {
        csspp::error_happened_t happened;

        csspp::error::instance().set_verbose(true);
        csspp::error::instance() << p << "verbose message to debug the compiler."
                                 << csspp::error_mode_t::ERROR_INFO;
        VERIFY_ERRORS("");
        CATCH_REQUIRE(error_count == csspp::error::instance().get_error_count());
        CATCH_REQUIRE(warning_count == csspp::error::instance().get_warning_count());

        CATCH_REQUIRE_FALSE(happened.error_happened());
        CATCH_REQUIRE_FALSE(happened.warning_happened());

        csspp::error::instance().set_verbose(false);
    }

    csspp::error::instance().set_hide_all(false);

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Error stream", "[error] [stream]")
{
    {
        std::stringstream ss;
        std::ostream & errout(csspp::error::instance().get_error_stream());
        CATCH_REQUIRE(&errout != &ss);
        {
            csspp::safe_error_stream_t safe_stream(ss);
            CATCH_REQUIRE(&csspp::error::instance().get_error_stream() == &ss);
        }
        CATCH_REQUIRE(&csspp::error::instance().get_error_stream() != &ss);
        CATCH_REQUIRE(&csspp::error::instance().get_error_stream() == &errout);
    }
}

// vim: ts=4 sw=4 et
