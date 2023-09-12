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
 * \brief Test the lexer.cpp file.
 *
 * This test runs a battery of tests agains the lexer.cpp file to ensure
 * full coverage and many edge cases as expected by CSS 3.
 *
 * Note that CSS 3 is fully compatible with CSS 1 and 2.1. However, it does
 * not support exactly the same character sets. Also our version only supports
 * UTF-8 as input.
 */

// csspp
//
#include    <csspp/exception.h>
#include    <csspp/lexer.h>
#include    <csspp/unicode_range.h>


// self
//
#include    "catch_main.h"


// C++
//
#include    <iomanip>
#include    <iostream>
#include    <sstream>


// C
//
#include    <math.h>
#include    <string.h>


// last include
//
#include    <snapdev/poison.h>



namespace
{

} // no name namespace




CATCH_TEST_CASE("UTF-8 conversions", "[lexer] [unicode]")
{
    std::stringstream ss;
    csspp::position pos("test.css");
    csspp::lexer l(ss, pos);

    // first check all valid characters encoding
    for(csspp::wide_char_t i(0); i < 0x110000; ++i)
    {
        if((i >= 0xD000 && i <= 0xDFFF) // surrogate
        || (i & 0xFFFF) == 0xFFFE       // invalid
        || (i & 0xFFFF) == 0xFFFF)      // invalid
        {
            continue;
        }
        std::string const str(l.wctomb(i));
        csspp::wide_char_t const wc(l.mbtowc(str.c_str()));

        CATCH_REQUIRE(wc == i);
    }

    // make sure the test for the buffer size works as expected
    for(size_t i(0); i < 5; ++i)
    {
        char buf[6];
        csspp::wide_char_t const wc(rand() % 0x1FFFFF);
        CATCH_REQUIRE_THROWS_AS(l.wctomb(wc, buf, i), csspp::csspp_exception_overflow);
    }

    // make sure surrogates are not allowed
    for(csspp::wide_char_t i(0xD800); i <= 0xDFFF; ++i)
    {
        char buf[6];
        buf[0] = '?';
        l.wctomb(i, buf, sizeof(buf) / sizeof(buf[0]));
        CATCH_REQUIRE(buf[0] == '\0'); // make sure we get an empty string on errors
        VERIFY_ERRORS("test.css(1): error: surrogate characters cannot be encoded in UTF-8.\n");
    }

    // page 0 -- error is slightly different
    {
        char buf[6];
        // test FFFE
        buf[0] = '?';
        l.wctomb(0xFFFE, buf, sizeof(buf) / sizeof(buf[0]));
        CATCH_REQUIRE(buf[0] == '\0'); // make sure we get an empty string on errors
        VERIFY_ERRORS("test.css(1): error: characters 0xFFFE and 0xFFFF are not valid.\n");

        // test FFFF
        buf[0] = '?';
        l.wctomb(0xFFFF, buf, sizeof(buf) / sizeof(buf[0]));
        CATCH_REQUIRE(buf[0] == '\0'); // make sure we get an empty string on errors
        VERIFY_ERRORS("test.css(1): error: characters 0xFFFE and 0xFFFF are not valid.\n");
    }

    // page 1 to 16
    for(csspp::wide_char_t page(1); page <= 0x10; ++page)
    {
        char buf[6];
        // test <page>FFFE
        csspp::wide_char_t wc((page << 16) | 0xFFFE);
        buf[0] = '?';
        l.wctomb(wc, buf, sizeof(buf) / sizeof(buf[0]));
        CATCH_REQUIRE(buf[0] == '\0'); // make sure we get an empty string on errors
        VERIFY_ERRORS("test.css(1): error: any characters that end with 0xFFFE or 0xFFFF are not valid.\n");

        // test <page>FFFF
        wc = (page << 16) | 0xFFFF;
        buf[0] = '?';
        l.wctomb(wc, buf, sizeof(buf) / sizeof(buf[0]));
        CATCH_REQUIRE(buf[0] == '\0'); // make sure we get an empty string on errors
        VERIFY_ERRORS("test.css(1): error: any characters that end with 0xFFFE or 0xFFFF are not valid.\n");
    }

    // test 1,000 characters with a number that's too large
    for(int i(0); i < 1000; ++i)
    {
        csspp::wide_char_t wc(0);
        do
        {
            // make sure we get a 32 bit value, hitting all possible bits
            wc = rand() ^ (rand() << 16);
        }
        while(static_cast<csspp::wide_uchar_t>(wc) < 0x110000);
        char buf[6];
        buf[0] = '?';
        l.wctomb(wc, buf, sizeof(buf) / sizeof(buf[0]));
        CATCH_REQUIRE(buf[0] == '\0'); // make sure we get an empty string on errors
        VERIFY_ERRORS("test.css(1): error: character too large, it cannot be encoded in UTF-8.\n");
    }

    // check that bytes 0xF8 and over generate an error
    for(int i(0xF8); i < 0x100; ++i)
    {
        char buf[6];
        buf[0] = i;
        buf[1] = static_cast<char>(0x80);
        buf[2] = static_cast<char>(0x80);
        buf[3] = static_cast<char>(0x80);
        buf[4] = static_cast<char>(0x80);
        buf[5] = 0;
        CATCH_REQUIRE(l.mbtowc(buf) == 0xFFFD);
        std::stringstream errmsg;
        errmsg << "test.css(1): error: byte U+" << std::hex << i << " not valid in a UTF-8 stream.\n";
        VERIFY_ERRORS(errmsg.str());
    }

    // continuation bytes at the start
    for(int i(0x80); i < 0xC0; ++i)
    {
        char buf[6];
        buf[0] = i;
        buf[1] = static_cast<char>(0x80);
        buf[2] = static_cast<char>(0x80);
        buf[3] = static_cast<char>(0x80);
        buf[4] = static_cast<char>(0x80);
        buf[5] = 0;
        CATCH_REQUIRE(l.mbtowc(buf) == 0xFFFD);
        std::stringstream errmsg;
        errmsg << "test.css(1): error: byte U+" << std::hex << i << " not valid to introduce a UTF-8 encoded character.\n";
        VERIFY_ERRORS(errmsg.str());
    }

    // not enough bytes ('\0' too soon)
    for(int i(0xC0); i < 0xF8; ++i)
    {
        char buf[6];
        buf[0] = i;
        buf[1] = 0;
        CATCH_REQUIRE(l.mbtowc(buf) == 0xFFFD);
        std::stringstream errmsg;
        errmsg << "test.css(1): error: sequence of bytes too short to represent a valid UTF-8 encoded character.\n";
        VERIFY_ERRORS(errmsg.str());
    }

    // too many bytes ('\0' missing), and
    // invalid sequence (a char replace with an invalid code)
    for(csspp::wide_char_t i(128); i < 0x110000; ++i)
    {
        if((i >= 0xD000 && i <= 0xDFFF) // surrogate
        || (i & 0xFFFF) == 0xFFFE       // invalid
        || (i & 0xFFFF) == 0xFFFF)      // invalid
        {
            continue;
        }
        std::string const str(l.wctomb(i));

        // the sequence is one too many bytes
        {
            std::string const too_long(str + static_cast<char>(rand() % 64 + 0x80)); // add one byte
            CATCH_REQUIRE(l.mbtowc(too_long.c_str()) == 0xFFFD);
            std::stringstream errmsg;
            errmsg << "test.css(1): error: sequence of bytes too long, it cannot represent a valid UTF-8 encoded character.\n";
            VERIFY_ERRORS(errmsg.str());
        }

        // one of the bytes in the sequence is not between 0x80 and 0xBF
        // this only works if the sequence is at least 2 bytes
        // (i.e. that starts when 'i' is 128 or more)
        {
            size_t const p(rand() % (str.length() - 1));
            if(p + 1 >= str.length())
            {
                std::cerr << "computed " << p + 1 << " for a string of length " << str.length() << "\n";
                throw std::logic_error("test computed a position that's out of range.");
            }
            std::string wrong_sequence(str);
            int c(rand() % (255 - 64) + 1); // '\0' is not good for this test
            if(c >= 0x80)
            {
                c += 64;
            }
            wrong_sequence[p + 1] = static_cast<char>(c);
            CATCH_REQUIRE(l.mbtowc(wrong_sequence.c_str()) == 0xFFFD);
            std::stringstream errmsg;
            errmsg << "test.css(1): error: invalid sequence of bytes, it cannot represent a valid UTF-8 encoded character.\n";
            VERIFY_ERRORS(errmsg.str());
        }
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Invalid characters", "[lexer] [invalid]")
{
    // 0xFFFD
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << l.wctomb(0xFFFD);

        // so far, no error
        VERIFY_ERRORS("");

        // EOF
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("test.css(1): error: invalid input character: U+fffd.\n");
    }

    // '\0'
    {
        std::stringstream ss;
        ss << '\0';
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // so far, no error
        VERIFY_ERRORS("");

        // EOF
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("test.css(1): error: invalid input character: U+fffd.\n");
    }

    // '^', '<', etc.
    for(int i(1); i < 128; ++i)
    {
        // test all that are invalid so we have an if to skip on all
        // characters that can represent the beginning of a valid token
        if(
            // whitespace
               i != ' '
            && i != '\t'
            && i != '\n'
            && i != '\r'
            && i != '\f'
            // identifiers / numbers
            && (i < '0' || i > '9')
            && (i < 'A' || i > 'Z')
            && (i < 'a' || i > 'z')
            && i != '_'
            && i != '-'
            && i != '+'
            && i != '.'
            && i != '\\'
            && i != '@'
            && i != '#'
            // delimiters
            && i != '*'
            && i != '/'
            && i != '='
            && i != '('
            && i != ')'
            && i != '{'
            && i != '}'
            && i != '['
            && i != ']'
            && i != ','
            && i != ';'
            && i != ':'
            && i != '<'
            && i != '>'
            && i != '$'
            && i != '!'
            && i != '|'
            && i != '&'
            && i != '~'
            && i != '%'
            && i != '?'
            // string
            && i != '"'
            && i != '\''
        )
        {
            std::stringstream ss;
            ss << static_cast<char>(i);
            csspp::position pos("test.css");
            csspp::lexer l(ss, pos);

            // so far, no error
            VERIFY_ERRORS("");

            // EOF
            CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

            // make sure we got the expected error
            std::stringstream errmsg;
            errmsg << "test.css(1): error: invalid input character: U+" << std::hex << static_cast<int>(i) << "." << std::endl;
            VERIFY_ERRORS(errmsg.str());
        }
    }

    // invalid UTF-8 sequence (i.e. too long)
    {
        std::stringstream ss;
        ss << static_cast<char>(0xF7)
           << static_cast<char>(0x80)
           << static_cast<char>(0x80)
           << static_cast<char>(0x80)
           << static_cast<char>(0x80)
           << static_cast<char>(0x80)
           << static_cast<char>(0x80)
           << static_cast<char>(0x80)
           << static_cast<char>(0x80);
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // so far, no error
        VERIFY_ERRORS("");

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected errors
        VERIFY_ERRORS(
                "test.css(1): error: too many follow bytes, it cannot represent a valid UTF-8 character.\n"
                "test.css(1): error: invalid input character: U+fffd.\n"
            );
    }

    // invalid UTF-8 sequence (i.e. too long, followed by a comment and a string)
    {
        std::stringstream ss;
        ss << static_cast<char>(0xF7)
           << static_cast<char>(0x80)
           << static_cast<char>(0x80)
           << static_cast<char>(0x80)
           << static_cast<char>(0x80)
           << static_cast<char>(0x80)
           << static_cast<char>(0x80)
           << static_cast<char>(0x80)
           << static_cast<char>(0x80)
           << "// plus a comment to @preserve\r\n"
           << "' and  a  string '";
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // so far, no error
        VERIFY_ERRORS("");

        // comment
        {
            csspp::node::pointer_t comment(l.next_token());
            CATCH_REQUIRE(comment->is(csspp::node_type_t::COMMENT));
            CATCH_REQUIRE(comment->get_string() == "plus a comment to @preserve");
            CATCH_REQUIRE(comment->get_integer() == 0); // C++ comment
            csspp::position const & npos(comment->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);

            // make sure we got the expected errors
            VERIFY_ERRORS(
                    "test.css(1): error: too many follow bytes, it cannot represent a valid UTF-8 character.\n"
                    "test.css(1): error: invalid input character: U+fffd.\n"
                    "test.css(1): warning: C++ comments should not be preserved as they are not supported by most CSS parsers.\n"
                );
        }

        // whitespace
        {
            csspp::node::pointer_t whitespace(l.next_token());
            CATCH_REQUIRE(whitespace->is(csspp::node_type_t::WHITESPACE));
            csspp::position const & npos(whitespace->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 2);
            CATCH_REQUIRE(npos.get_total_line() == 2);
        }

        // string
        {
            csspp::node::pointer_t string(l.next_token());
            CATCH_REQUIRE(string->is(csspp::node_type_t::STRING));
            CATCH_REQUIRE(string->get_string() == " and  a  string ");
            csspp::position const & npos(string->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 2);
            CATCH_REQUIRE(npos.get_total_line() == 2);
        }

        // EOF
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
    }

    // invalid UTF-8 sequence (i.e. incorrect introducer)
    for(int i(0x80); i <= 0xC0; ++i)
    {
        // ends with EOF
        {
            std::stringstream ss;
            ss << static_cast<char>(i == 0xC0 ? 0xFF : i)
               << static_cast<char>(0x80)
               << static_cast<char>(0x80)
               << static_cast<char>(0x80)
               << static_cast<char>(0x80)
               << static_cast<char>(0x80)
               << static_cast<char>(0x80)
               << static_cast<char>(0x80)
               << static_cast<char>(0x80);
            csspp::position pos("test.css");
            csspp::lexer l(ss, pos);

            // so far, no error
            VERIFY_ERRORS("");

            CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

            // make sure we got the expected errors
            std::stringstream errmsg;
            errmsg << "test.css(1): error: unexpected byte in input buffer: U+"
                   << std::hex << (i == 0xC0 ? 0xFF : i)
                   << ".\ntest.css(1): error: invalid input character: U+fffd.\n";
            VERIFY_ERRORS(errmsg.str());
        }

        // ends with comment and string
        {
            std::stringstream ss;
            ss << static_cast<char>(i == 0xC0 ? 0xFF : i)
               << static_cast<char>(0x80)
               << static_cast<char>(0x80)
               << static_cast<char>(0x80)
               << static_cast<char>(0x80)
               << static_cast<char>(0x80)
               << static_cast<char>(0x80)
               << static_cast<char>(0x80)
               << static_cast<char>(0x80)
               << "// plus one comment to @preserve\r\n"
               << "' and  that  string '";
            csspp::position pos("test.css");
            csspp::lexer l(ss, pos);

            // so far, no error
            VERIFY_ERRORS("");

            // comment
            {
                csspp::node::pointer_t comment(l.next_token());
                CATCH_REQUIRE(comment->is(csspp::node_type_t::COMMENT));
                CATCH_REQUIRE(comment->get_string() == "plus one comment to @preserve");
                CATCH_REQUIRE(comment->get_integer() == 0); // C++ comment
                csspp::position const & npos(comment->get_position());
                CATCH_REQUIRE(npos.get_filename() == "test.css");
                CATCH_REQUIRE(npos.get_page() == 1);
                CATCH_REQUIRE(npos.get_line() == 1);
                CATCH_REQUIRE(npos.get_total_line() == 1);
            }

            // whitespace
            {
                csspp::node::pointer_t whitespace(l.next_token());
                CATCH_REQUIRE(whitespace->is(csspp::node_type_t::WHITESPACE));
                csspp::position const & npos(whitespace->get_position());
                CATCH_REQUIRE(npos.get_filename() == "test.css");
                CATCH_REQUIRE(npos.get_page() == 1);
                CATCH_REQUIRE(npos.get_line() == 2);
                CATCH_REQUIRE(npos.get_total_line() == 2);
            }

            // string
            {
                csspp::node::pointer_t string(l.next_token());
                CATCH_REQUIRE(string->is(csspp::node_type_t::STRING));
                CATCH_REQUIRE(string->get_string() == " and  that  string ");
                csspp::position const & npos(string->get_position());
                CATCH_REQUIRE(npos.get_filename() == "test.css");
                CATCH_REQUIRE(npos.get_page() == 1);
                CATCH_REQUIRE(npos.get_line() == 2);
                CATCH_REQUIRE(npos.get_total_line() == 2);
            }

            // EOF
            CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

            // make sure we got the expected errors
            std::stringstream errmsg;
            errmsg << "test.css(1): error: unexpected byte in input buffer: U+"
                   << std::hex << (i == 0xC0 ? 0xFF : i)
                   << ".\ntest.css(1): error: invalid input character: U+fffd.\n"
                   << "test.css(1): warning: C++ comments should not be preserved as they are not supported by most CSS parsers.\n";
            VERIFY_ERRORS(errmsg.str());
        }
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Simple tokens", "[lexer] [basics] [delimiters]")
{
    // ' ' -> WHITESPACE
    {
        char const * whitespaces = " \t\n\r\f";
        size_t len(strlen(whitespaces));

        // try evey single whitespace by itself
        for(size_t i(0); i < len; ++i)
        {
            std::stringstream ss;
            csspp::position pos("test.css");
            csspp::lexer l(ss, pos);
            ss << whitespaces[i];

            // so far, no error
            VERIFY_ERRORS("");

            CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::WHITESPACE));
            CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

            // make sure we got the expected error
            VERIFY_ERRORS("");
        }

        // try 1,000 combo of 3 to 12 whitespaces
        for(size_t i(0); i < 1000; ++i)
        {
            std::stringstream ss;
            csspp::position pos("test.css");
            csspp::lexer l(ss, pos);
            size_t const count(rand() % 10 + 3);
            for(size_t j(0); j < count; ++j)
            {
                int const k(rand() % len);
                ss << whitespaces[k];
            }

            // so far, no error
            VERIFY_ERRORS("");

            CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::WHITESPACE));
            CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

            // make sure we got the expected error
            VERIFY_ERRORS("");
        }
    }

    // '=' -> EQUAL
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << "=";

        // so far, no error
        VERIFY_ERRORS("");

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EQUAL));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("");
    }

    // '==' -> EQUAL + warning
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << "==";

        // so far, no error
        VERIFY_ERRORS("");

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EQUAL));
        VERIFY_ERRORS("test.css(1): warning: we accepted '==' instead of '=' in an expression, you probably want to change the operator to just '=', though.\n");
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("");
    }

    // ',' -> COMMA
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << ",";

        // so far, no error
        VERIFY_ERRORS("");

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::COMMA));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("");
    }

    // ':' -> COLON
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << ":";

        // so far, no error
        VERIFY_ERRORS("");

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::COLON));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("");
    }

    // ';' -> SEMICOLON
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << ";";

        // so far, no error
        VERIFY_ERRORS("");

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::SEMICOLON));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("");
    }

    // '.' -> PERIOD
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << ".";

        // so far, no error
        VERIFY_ERRORS("");

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::PERIOD));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("");
    }

    // '$' -> DOLLAR
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << "$";

        // so far, no error
        VERIFY_ERRORS("");

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::DOLLAR));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("");
    }

    // '?' -> CONDITIONAL
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << "?";

        // so far, no error
        VERIFY_ERRORS("");

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::CONDITIONAL));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("");
    }

    // '%' -> MODULO
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << "%";

        // so far, no error
        VERIFY_ERRORS("");

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::MODULO));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("");
    }

    // '/' -> DIVIDE
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << "/";

        // so far, no error
        VERIFY_ERRORS("");

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::DIVIDE));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("");
    }

    // '*' -> MULTIPLY
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << "*";

        // so far, no error
        VERIFY_ERRORS("");

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::MULTIPLY));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("");
    }

    // '**' -> POWER
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << '*' << '*';

        // so far, no error
        VERIFY_ERRORS("");

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::POWER));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("");
    }

    // '&' -> REFERENCE
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << "&";

        // so far, no error
        VERIFY_ERRORS("");

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::REFERENCE));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("");
    }

    // '&&' -> AND
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << '&' << '&';

        // so far, no error
        VERIFY_ERRORS("");

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::AND));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("");
    }

    // '~' -> PRECEDED
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << "~";

        // so far, no error
        VERIFY_ERRORS("");

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::PRECEDED));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("");
    }

    // '+' -> ADD
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << "+";

        // so far, no error
        VERIFY_ERRORS("");

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::ADD));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("");
    }

    // '-' -> SUBTRACT
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << "-";

        // so far, no error
        VERIFY_ERRORS("");

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::SUBTRACT));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("");
    }

    // '|' -> SCOPE
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << "|";

        // so far, no error
        VERIFY_ERRORS("");

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::SCOPE));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("");
    }

    // '>' -> GREATER_THAN
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << ">";

        // so far, no error
        VERIFY_ERRORS("");

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::GREATER_THAN));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("");
    }

    // '>=' -> GREATER_EQUAL
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << ">=";

        // so far, no error
        VERIFY_ERRORS("");

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::GREATER_EQUAL));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("");
    }

    // "<"
    {
        std::stringstream ss;
        ss << '<';
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // so far, no error
        VERIFY_ERRORS("");

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::LESS_THAN));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("");
    }

    // "<="
    {
        std::stringstream ss;
        ss << '<' << '=';
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // so far, no error
        VERIFY_ERRORS("");

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::LESS_EQUAL));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("");
    }

    // "<!" -- (special case because of "<!--")
    {
        std::stringstream ss;
        ss << '<' << '!';
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // so far, no error
        VERIFY_ERRORS("");

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::LESS_THAN));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EXCLAMATION));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("");
    }

    // "<!-" -- (special case because of "<!--")
    {
        std::stringstream ss;
        ss << '<' << '!' << '-';
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // so far, no error
        VERIFY_ERRORS("");

        // The '<' is returned as LESS_THAN
        // The '!' is returned as EXCLAMATION
        // The '-' is returned as SUBTRACT
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::LESS_THAN));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EXCLAMATION));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::SUBTRACT));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("");
    }

    // "!"
    {
        std::stringstream ss;
        ss << '!';
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // so far, no error
        VERIFY_ERRORS("");

        // The '!' is returned as EXCLAMATION
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EXCLAMATION));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("");
    }

    // "!="
    {
        std::stringstream ss;
        ss << '!' << '=';
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // so far, no error
        VERIFY_ERRORS("");

        // The '!=' is returned as NOT_EQUAL
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::NOT_EQUAL));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("");
    }

    // '(' -> OPEN_PARENTHESIS
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << "(";

        // so far, no error
        VERIFY_ERRORS("");

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::OPEN_PARENTHESIS));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("");
    }

    // ')' -> OPEN_PARENTHESIS
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << ")";

        // so far, no error
        VERIFY_ERRORS("");

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::CLOSE_PARENTHESIS));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("");
    }

    // '{' -> OPEN_CURLYBRACKET
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << "{";

        // so far, no error
        VERIFY_ERRORS("");

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::OPEN_CURLYBRACKET));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("");
    }

    // '}' -> OPEN_CURLYBRACKET
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << "}";

        // so far, no error
        VERIFY_ERRORS("");

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::CLOSE_CURLYBRACKET));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("");
    }

    // '[' -> OPEN_SQUAREBRACKET
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << "[";

        // so far, no error
        VERIFY_ERRORS("");

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::OPEN_SQUAREBRACKET));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("");
    }

    // ']' -> OPEN_SQUAREBRACKET
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << "]";

        // so far, no error
        VERIFY_ERRORS("");

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::CLOSE_SQUAREBRACKET));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("");
    }

    // '<!--' -> CDO
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << "<!--";

        // so far, no error
        VERIFY_ERRORS("");

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::CDO));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("");
    }

    // '<!--' -> CDC
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << "-->";

        // so far, no error
        VERIFY_ERRORS("");

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::CDC));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("");
    }

    // '^=' -> PREFIX_MATCH
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << "^=";

        // so far, no error
        VERIFY_ERRORS("");

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::PREFIX_MATCH));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("");
    }

    // '|=' -> DASH_MATCH
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << "|=";

        // so far, no error
        VERIFY_ERRORS("");

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::DASH_MATCH));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("");
    }

    // '$=' -> SUFFIX_MATCH
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << "$=";

        // so far, no error
        VERIFY_ERRORS("");

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::SUFFIX_MATCH));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("");
    }

    // '~=' -> INCLUDE_MATCH
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << "~=";

        // so far, no error
        VERIFY_ERRORS("");

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::INCLUDE_MATCH));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("");
    }

    // '*=' -> SUBSTRING_MATCH
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << "*=";

        // so far, no error
        VERIFY_ERRORS("");

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::SUBSTRING_MATCH));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("");
    }

    // ':=' -> ASSIGNMENT
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << ":=";

        // so far, no error
        VERIFY_ERRORS("");

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::ASSIGNMENT));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("");
    }

    // '||' -> COLUMN
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << "||";

        // so far, no error
        VERIFY_ERRORS("");

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::COLUMN));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("");
    }

    // A "special" sequence div+.alpha
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << "div+.alpha";

        // so far, no error
        VERIFY_ERRORS("");

        csspp::node::pointer_t div(l.next_token());
        CATCH_REQUIRE(div->is(csspp::node_type_t::IDENTIFIER));
        CATCH_REQUIRE(div->get_string() == "div");

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::ADD));

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::PERIOD));

        csspp::node::pointer_t alpha(l.next_token());
        CATCH_REQUIRE(alpha->is(csspp::node_type_t::IDENTIFIER));
        CATCH_REQUIRE(alpha->get_string() == "alpha");

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("");
    }

    // A "special" sequence div -.alpha
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << "div -.alpha";

        // so far, no error
        VERIFY_ERRORS("");

        csspp::node::pointer_t div(l.next_token());
        CATCH_REQUIRE(div->is(csspp::node_type_t::IDENTIFIER));
        CATCH_REQUIRE(div->get_string() == "div");

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::WHITESPACE));

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::SUBTRACT));

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::PERIOD));

        csspp::node::pointer_t alpha(l.next_token());
        CATCH_REQUIRE(alpha->is(csspp::node_type_t::IDENTIFIER));
        CATCH_REQUIRE(alpha->get_string() == "alpha");

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // make sure we got the expected error
        VERIFY_ERRORS("");
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Newline", "[lexer] [newline] [characters]")
{
    // we have a special case with '\r' followed by a character
    // other than '\n'
    for(csspp::wide_char_t i(0x80); i < 0x110000; ++i)
    {
        switch(i)
        {
        case 0xFFFD:
        case 0x00FFFE:
        case 0x00FFFF:
        case 0x01FFFE:
        case 0x01FFFF:
        case 0x02FFFE:
        case 0x02FFFF:
        case 0x03FFFE:
        case 0x03FFFF:
        case 0x04FFFE:
        case 0x04FFFF:
        case 0x05FFFE:
        case 0x05FFFF:
        case 0x06FFFE:
        case 0x06FFFF:
        case 0x07FFFE:
        case 0x07FFFF:
        case 0x08FFFE:
        case 0x08FFFF:
        case 0x09FFFE:
        case 0x09FFFF:
        case 0x0AFFFE:
        case 0x0AFFFF:
        case 0x0BFFFE:
        case 0x0BFFFF:
        case 0x0CFFFE:
        case 0x0CFFFF:
        case 0x0DFFFE:
        case 0x0DFFFF:
        case 0x0EFFFE:
        case 0x0EFFFF:
        case 0x0FFFFE:
        case 0x0FFFFF:
        case 0x10FFFE:
        case 0x10FFFF:
            // skip on characters that are either invalid or generate
            // a "problem" (i.e. spaces get trimmed)
            continue;

        default:
            if(i >= 0xD800 &&  i <= 0xDFFF)
            {
                continue;
            }
            break;

        }

        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << '\r' << l.wctomb(i)
           << '\n' << l.wctomb(i)
           << '\f' << l.wctomb(i)
           << "\r\n" << l.wctomb(i)
           << "\n\r" << l.wctomb(i);

        std::stringstream out;
        out << l.wctomb(i);

        // character on the second line (\r char)
        {
            // check the whitespace
            csspp::node::pointer_t whitespace(l.next_token());
            CATCH_REQUIRE(whitespace->is(csspp::node_type_t::WHITESPACE));
            csspp::position const & npos(whitespace->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // make sure the next character is viewed as an identifier
        // just as expected
        {
            csspp::node::pointer_t identifier(l.next_token());
            CATCH_REQUIRE(identifier->is(csspp::node_type_t::IDENTIFIER));
            CATCH_REQUIRE(identifier->get_string() == out.str());
            csspp::position const & npos(identifier->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 2);
            CATCH_REQUIRE(npos.get_total_line() == 2);
        }

        // character on the second line (\n char)
        {
            // check the whitespace
            csspp::node::pointer_t whitespace(l.next_token());
            CATCH_REQUIRE(whitespace->is(csspp::node_type_t::WHITESPACE));
            csspp::position const & npos(whitespace->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 3);
            CATCH_REQUIRE(npos.get_total_line() == 3);
        }

        // make sure the next character is viewed as an identifier
        // just as expected
        {
            csspp::node::pointer_t identifier(l.next_token());
            CATCH_REQUIRE(identifier->is(csspp::node_type_t::IDENTIFIER));
            CATCH_REQUIRE(identifier->get_string() == out.str());
            csspp::position const & npos(identifier->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 3);
            CATCH_REQUIRE(npos.get_total_line() == 3);
        }

        // character on the second line (\f char)
        {
            // check the whitespace
            csspp::node::pointer_t whitespace(l.next_token());
            CATCH_REQUIRE(whitespace->is(csspp::node_type_t::WHITESPACE));
            csspp::position const & npos(whitespace->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 2);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 3);
        }

        // make sure the next character is viewed as an identifier
        // just as expected
        {
            csspp::node::pointer_t identifier(l.next_token());
            CATCH_REQUIRE(identifier->is(csspp::node_type_t::IDENTIFIER));
            CATCH_REQUIRE(identifier->get_string() == out.str());
            csspp::position const & npos(identifier->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 2);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 3);
        }

        // character on the second line (\r\n char)
        {
            // check the whitespace
            csspp::node::pointer_t whitespace(l.next_token());
            CATCH_REQUIRE(whitespace->is(csspp::node_type_t::WHITESPACE));
            csspp::position const & npos(whitespace->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 2);
            CATCH_REQUIRE(npos.get_line() == 2);
            CATCH_REQUIRE(npos.get_total_line() == 4);
        }

        // make sure the next character is viewed as an identifier
        // just as expected
        {
            csspp::node::pointer_t identifier(l.next_token());
            CATCH_REQUIRE(identifier->is(csspp::node_type_t::IDENTIFIER));
            CATCH_REQUIRE(identifier->get_string() == out.str());
            csspp::position const & npos(identifier->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 2);
            CATCH_REQUIRE(npos.get_line() == 2);
            CATCH_REQUIRE(npos.get_total_line() == 4);
        }

        // character on the second line (\n\r char)
        {
            // check the whitespace
            csspp::node::pointer_t whitespace(l.next_token());
            CATCH_REQUIRE(whitespace->is(csspp::node_type_t::WHITESPACE));
            csspp::position const & npos(whitespace->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 2);
            CATCH_REQUIRE(npos.get_line() == 3);
            CATCH_REQUIRE(npos.get_total_line() == 5);
        }

        // make sure the next character is viewed as an identifier
        // just as expected
        {
            csspp::node::pointer_t identifier(l.next_token());
            CATCH_REQUIRE(identifier->is(csspp::node_type_t::IDENTIFIER));
            CATCH_REQUIRE(identifier->get_string() == out.str());
            csspp::position const & npos(identifier->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 2);
            CATCH_REQUIRE(npos.get_line() == 4);
            CATCH_REQUIRE(npos.get_total_line() == 6);
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("C-like comments", "[lexer] [comment]")
{
    // a comment without @preserve gets lost
    {
        std::stringstream ss;
        ss << "/* test simple comment */";
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
    }

    // one simple comment
    {
        std::stringstream ss;
        ss << "/* test simple comment @preserve */";
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // comment
        {
            csspp::node::pointer_t comment(l.next_token());
            CATCH_REQUIRE(comment->is(csspp::node_type_t::COMMENT));
            CATCH_REQUIRE(comment->get_string() == "test simple comment @preserve");
            CATCH_REQUIRE(comment->get_integer() == 1); // C-like comment
            csspp::position const & npos(comment->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
    }

    // an unterminated simple comment
    {
        std::stringstream ss;
        ss << "/* test simple comment @preserve";
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // comment
        {
            csspp::node::pointer_t comment(l.next_token());
            CATCH_REQUIRE(comment->is(csspp::node_type_t::COMMENT));
            CATCH_REQUIRE(comment->get_string() == "test simple comment @preserve");
            CATCH_REQUIRE(comment->get_integer() == 1); // C-like comment
            csspp::position const & npos(comment->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        VERIFY_ERRORS("test.css(1): error: unclosed C-like comment at the end of your document.\n");
    }

    // a comment on multiple lines
    {
        std::stringstream ss;
        ss << "/* test\na\r\nmulti-line\fcomment\n\rtoo @preserve */\n";
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // comment
        {
            csspp::node::pointer_t comment(l.next_token());
            CATCH_REQUIRE(comment->is(csspp::node_type_t::COMMENT));
            CATCH_REQUIRE(comment->get_string() == "test\na\nmulti-line\ncomment\n\ntoo @preserve");
            CATCH_REQUIRE(comment->get_integer() == 1); // C-like comment
            csspp::position const & npos(comment->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // whitespace
        {
            csspp::node::pointer_t whitespace(l.next_token());
            CATCH_REQUIRE(whitespace->is(csspp::node_type_t::WHITESPACE));
            csspp::position const & npos(whitespace->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 2);
            CATCH_REQUIRE(npos.get_line() == 3);
            CATCH_REQUIRE(npos.get_total_line() == 5);
        }

        // EOF
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
    }

    // one multi-line comment followed by another simple comment
    {
        std::stringstream ss;
        ss << "/* test\na\r\nmulti-line\fcomment\n\rtoo @preserve */\n/* with a second comment @preserve */";
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // 1st comment
        {
            csspp::node::pointer_t comment1(l.next_token());
            CATCH_REQUIRE(comment1->is(csspp::node_type_t::COMMENT));
            CATCH_REQUIRE(comment1->get_string() == "test\na\nmulti-line\ncomment\n\ntoo @preserve");
            CATCH_REQUIRE(comment1->get_integer() == 1); // C-like comment
            csspp::position const & npos(comment1->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // whitespace in between
        {
            csspp::node::pointer_t whitespace(l.next_token());
            csspp::position const & npos(whitespace->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 2);
            CATCH_REQUIRE(npos.get_line() == 3);
            CATCH_REQUIRE(npos.get_total_line() == 5);
        }

        // 2nd comment
        {
            csspp::node::pointer_t comment(l.next_token());
            CATCH_REQUIRE(comment->is(csspp::node_type_t::COMMENT));
            CATCH_REQUIRE(comment->get_string() == "with a second comment @preserve");
            CATCH_REQUIRE(comment->get_integer() == 1); // C-like comment
            csspp::position const & npos(comment->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 2);
            CATCH_REQUIRE(npos.get_line() == 4);
            CATCH_REQUIRE(npos.get_total_line() == 6);
        }

        // EOF
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
    }

    // test with all types of characters that are considered valid by
    // our code
    {
        for(csspp::wide_char_t i(1); i < 0x110000; ++i)
        {
            switch(i)
            {
            case ' ':
            case '\t':
            case '\n':
            case '\r':
            case '\f':
            case '\0':
            case 0xFFFD:
            case 0x00FFFE:
            case 0x00FFFF:
            case 0x01FFFE:
            case 0x01FFFF:
            case 0x02FFFE:
            case 0x02FFFF:
            case 0x03FFFE:
            case 0x03FFFF:
            case 0x04FFFE:
            case 0x04FFFF:
            case 0x05FFFE:
            case 0x05FFFF:
            case 0x06FFFE:
            case 0x06FFFF:
            case 0x07FFFE:
            case 0x07FFFF:
            case 0x08FFFE:
            case 0x08FFFF:
            case 0x09FFFE:
            case 0x09FFFF:
            case 0x0AFFFE:
            case 0x0AFFFF:
            case 0x0BFFFE:
            case 0x0BFFFF:
            case 0x0CFFFE:
            case 0x0CFFFF:
            case 0x0DFFFE:
            case 0x0DFFFF:
            case 0x0EFFFE:
            case 0x0EFFFF:
            case 0x0FFFFE:
            case 0x0FFFFF:
            case 0x10FFFE:
            case 0x10FFFF:
                // skip on characters that are either invalid or generate
                // a "problem" (i.e. spaces get trimmed)
                continue;

            default:
                if(i >= 0xD800 &&  i <= 0xDFFF)
                {
                    continue;
                }
                break;

            }

            std::stringstream ss;
            char mb[6];
            csspp::position pos("test.css");
            csspp::lexer l(ss, pos);
            l.wctomb(i, mb, sizeof(mb) / sizeof(mb[0]));
//std::cerr << "testing with " << i << "\n";
//for(int j(0); mb[j] != '\0'; ++j) std::cerr << " " << j << ". " << std::hex << static_cast<int>(static_cast<unsigned char>(mb[j])) << std::dec << "\n";
            std::string cmt("character: ");
            cmt += mb;
            ss << "/* " << cmt << " @preserve */";

            // comment
            {
                csspp::node::pointer_t comment(l.next_token());
                CATCH_REQUIRE(comment->is(csspp::node_type_t::COMMENT));
                CATCH_REQUIRE(comment->get_string() == cmt + " @preserve");
                CATCH_REQUIRE(comment->get_integer() == 1); // C-like comment
                csspp::position const & npos(comment->get_position());
                CATCH_REQUIRE(npos.get_filename() == "test.css");
                CATCH_REQUIRE(npos.get_page() == 1);
                CATCH_REQUIRE(npos.get_line() == 1);
                CATCH_REQUIRE(npos.get_total_line() == 1);
            }

            CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
            CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
            CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
        }
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("C++ comments", "[lexer] [comment]")
{
    // a comment without @preserve gets lost
    {
        std::stringstream ss;
        ss << "// test simple comment\r\n";
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // whitespace
        {
            csspp::node::pointer_t whitespace(l.next_token());
            CATCH_REQUIRE(whitespace->is(csspp::node_type_t::WHITESPACE));
            csspp::position const & npos(whitespace->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 2);
            CATCH_REQUIRE(npos.get_total_line() == 2);
        }

        // EOF
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // no error left over
        VERIFY_ERRORS("");
    }

    // one simple comment
    {
        std::stringstream ss;
        ss << "// test simple comment @preserve\r\n";
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // comment
        {
            csspp::node::pointer_t comment(l.next_token());
            CATCH_REQUIRE(comment->is(csspp::node_type_t::COMMENT));
            CATCH_REQUIRE(comment->get_string() == "test simple comment @preserve");
            CATCH_REQUIRE(comment->get_integer() == 0); // C++ comment
            csspp::position const & npos(comment->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);

            VERIFY_ERRORS("test.css(1): warning: C++ comments should not be preserved as they are not supported by most CSS parsers.\n");
        }

        // whitespace
        {
            csspp::node::pointer_t whitespace(l.next_token());
            CATCH_REQUIRE(whitespace->is(csspp::node_type_t::WHITESPACE));
            csspp::position const & npos(whitespace->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 2);
            CATCH_REQUIRE(npos.get_total_line() == 2);
        }

        // EOF
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // no error left over
        VERIFY_ERRORS("");
    }

    // a C++ comment on multiple lines is just a comment
    // that is followed by a number of other C++ comments
    {
        std::stringstream ss;
        ss << "// test\n// a\r\n// multi-line\f//comment\r//\ttoo @preserve\n";
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // comment
        {
            csspp::node::pointer_t comment(l.next_token());
            CATCH_REQUIRE(comment->is(csspp::node_type_t::COMMENT));
            CATCH_REQUIRE(comment->get_string() == "test\na\nmulti-line\ncomment\ntoo @preserve");
            CATCH_REQUIRE(comment->get_integer() == 0); // C++ comment
            csspp::position const & npos(comment->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);

            VERIFY_ERRORS("test.css(1): warning: C++ comments should not be preserved as they are not supported by most CSS parsers.\n");
        }

        // whitespace
        {
            csspp::node::pointer_t whitespace(l.next_token());
            CATCH_REQUIRE(whitespace->is(csspp::node_type_t::WHITESPACE));
            csspp::position const & npos(whitespace->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 2);
            CATCH_REQUIRE(npos.get_line() == 3);
            CATCH_REQUIRE(npos.get_total_line() == 5);
        }

        // EOF
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // no error left over
        VERIFY_ERRORS("");
    }

    // one multi-line comment followed by another simple comment
    {
        std::stringstream ss;
        ss << "// test\n//\ta\r\n//multi-line\f// comment\r\n// too @preserve\r\n\r\n// with a second comment @preserve";
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // 1st comment
        {
            csspp::node::pointer_t comment(l.next_token());
            CATCH_REQUIRE(comment->is(csspp::node_type_t::COMMENT));
            CATCH_REQUIRE(comment->get_string() == "test\na\nmulti-line\ncomment\ntoo @preserve");
            CATCH_REQUIRE(comment->get_integer() == 0); // C++ comment
            csspp::position const & npos(comment->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);

            VERIFY_ERRORS("test.css(1): warning: C++ comments should not be preserved as they are not supported by most CSS parsers.\n");
        }

        // whitespace
        {
            csspp::node::pointer_t whitespace(l.next_token());
            CATCH_REQUIRE(whitespace->is(csspp::node_type_t::WHITESPACE));
            csspp::position const & npos(whitespace->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 2);
            CATCH_REQUIRE(npos.get_line() == 4);
            CATCH_REQUIRE(npos.get_total_line() == 6);
        }

        // 2nd comment
        {
            csspp::node::pointer_t comment(l.next_token());
            CATCH_REQUIRE(comment->is(csspp::node_type_t::COMMENT));
            CATCH_REQUIRE(comment->get_string() == "with a second comment @preserve");
            CATCH_REQUIRE(comment->get_integer() == 0); // C++ comment
            csspp::position const & npos(comment->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 2);
            CATCH_REQUIRE(npos.get_line() == 4);
            CATCH_REQUIRE(npos.get_total_line() == 6);

            VERIFY_ERRORS("test.css(4): warning: C++ comments should not be preserved as they are not supported by most CSS parsers.\n");
        }

        // EOF
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // no error left over
        VERIFY_ERRORS("");
    }

    // one comment nearly multi-line
    {
        std::stringstream ss;
        ss << "// test comment and @preserve\n/ divide";
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // comment
        {
            csspp::node::pointer_t comment(l.next_token());
            CATCH_REQUIRE(comment->is(csspp::node_type_t::COMMENT));
            CATCH_REQUIRE(comment->get_string() == "test comment and @preserve");
            CATCH_REQUIRE(comment->get_integer() == 0); // C++ comment
            csspp::position const & npos(comment->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);

            VERIFY_ERRORS("test.css(1): warning: C++ comments should not be preserved as they are not supported by most CSS parsers.\n");
        }

        // whitespace
        {
            csspp::node::pointer_t whitespace(l.next_token());
            CATCH_REQUIRE(whitespace->is(csspp::node_type_t::WHITESPACE));
            csspp::position const & npos(whitespace->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 2);
            CATCH_REQUIRE(npos.get_total_line() == 2);
        }

        // divide
        {
            csspp::node::pointer_t comment(l.next_token());
            CATCH_REQUIRE(comment->is(csspp::node_type_t::DIVIDE));
            csspp::position const & npos(comment->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 2);
            CATCH_REQUIRE(npos.get_total_line() == 2);
        }

        // whitespace
        {
            csspp::node::pointer_t whitespace(l.next_token());
            CATCH_REQUIRE(whitespace->is(csspp::node_type_t::WHITESPACE));
            csspp::position const & npos(whitespace->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 2);
            CATCH_REQUIRE(npos.get_total_line() == 2);
        }

        // identifier
        {
            csspp::node::pointer_t identifier(l.next_token());
            CATCH_REQUIRE(identifier->is(csspp::node_type_t::IDENTIFIER));
            CATCH_REQUIRE(identifier->get_string() == "divide");
            csspp::position const & npos(identifier->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 2);
            CATCH_REQUIRE(npos.get_total_line() == 2);
        }

        // EOF
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // no error left over
        VERIFY_ERRORS("");
    }

    // test with all types of characters that are considered valid by
    // our code
    {
        for(csspp::wide_char_t i(1); i < 0x110000; ++i)
        {
            switch(i)
            {
            case ' ':
            case '\t':
            case '\n':
            case '\r':
            case '\f':
            case '\0':
            case 0xFFFD:
            case 0x00FFFE:
            case 0x00FFFF:
            case 0x01FFFE:
            case 0x01FFFF:
            case 0x02FFFE:
            case 0x02FFFF:
            case 0x03FFFE:
            case 0x03FFFF:
            case 0x04FFFE:
            case 0x04FFFF:
            case 0x05FFFE:
            case 0x05FFFF:
            case 0x06FFFE:
            case 0x06FFFF:
            case 0x07FFFE:
            case 0x07FFFF:
            case 0x08FFFE:
            case 0x08FFFF:
            case 0x09FFFE:
            case 0x09FFFF:
            case 0x0AFFFE:
            case 0x0AFFFF:
            case 0x0BFFFE:
            case 0x0BFFFF:
            case 0x0CFFFE:
            case 0x0CFFFF:
            case 0x0DFFFE:
            case 0x0DFFFF:
            case 0x0EFFFE:
            case 0x0EFFFF:
            case 0x0FFFFE:
            case 0x0FFFFF:
            case 0x10FFFE:
            case 0x10FFFF:
                // skip on characters that are either invalid or generate
                // a "problem" (i.e. spaces get trimmed)
                continue;

            default:
                if(i >= 0xD800 &&  i <= 0xDFFF)
                {
                    continue;
                }
                break;

            }

            std::stringstream ss;
            char mb[6];
            csspp::position pos("test.css");
            csspp::lexer l(ss, pos);
            l.wctomb(i, mb, sizeof(mb) / sizeof(mb[0]));
//std::cerr << "testing with " << i << "\n";
//for(int j(0); mb[j] != '\0'; ++j) std::cerr << " " << j << ". " << std::hex << static_cast<int>(static_cast<unsigned char>(mb[j])) << std::dec << "\n";
            std::string cmt("character: ");
            cmt += mb;
            ss << "// " << cmt << " @preserve";

            // comment
            {
                csspp::node::pointer_t comment(l.next_token());
                CATCH_REQUIRE(comment->is(csspp::node_type_t::COMMENT));
                CATCH_REQUIRE(comment->get_string() == cmt + " @preserve");
                CATCH_REQUIRE(comment->get_integer() == 0); // C++ comment
                csspp::position const & npos(comment->get_position());
                CATCH_REQUIRE(npos.get_filename() == "test.css");
                CATCH_REQUIRE(npos.get_page() == 1);
                CATCH_REQUIRE(npos.get_line() == 1);
                CATCH_REQUIRE(npos.get_total_line() == 1);

                VERIFY_ERRORS("test.css(1): warning: C++ comments should not be preserved as they are not supported by most CSS parsers.\n");
            }

            CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
            CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
            CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
        }

        // no error left over
        VERIFY_ERRORS("");
    }
}

CATCH_TEST_CASE("Strings", "[lexer] [string]")
{
    // one simple string with "
    {
        std::stringstream ss;
        ss << "\"";
        size_t const len(rand() % 20 + 20);
        std::string word;
        for(size_t i(0); i < len; ++i)
        {
            // simple ascii letters
            int const c(rand() % 26 + 'a');
            ss << static_cast<char>(c);
            word += static_cast<char>(c);
        }
        ss << "\"";
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // string
        {
            csspp::node::pointer_t string(l.next_token());
            CATCH_REQUIRE(string->is(csspp::node_type_t::STRING));
            CATCH_REQUIRE(string->get_string() == word);
            csspp::position const & npos(string->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // no error left over
        VERIFY_ERRORS("");
    }

    // one simple string with " and including '
    {
        std::stringstream ss;
        ss << "\"c'est un teste\"";
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // string
        {
            csspp::node::pointer_t string(l.next_token());
            CATCH_REQUIRE(string->is(csspp::node_type_t::STRING));
            CATCH_REQUIRE(string->get_string() == "c'est un teste");
            csspp::position const & npos(string->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // no error left over
        VERIFY_ERRORS("");
    }

    // one simple string with '
    {
        std::stringstream ss;
        ss << "'";
        size_t const len(rand() % 20 + 20);
        std::string word;
        for(size_t i(0); i < len; ++i)
        {
            // simple ascii letters
            int const c(rand() % 26 + 'a');
            ss << static_cast<char>(c);
            word += static_cast<char>(c);
        }
        ss << "'";
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // string
        {
            csspp::node::pointer_t string(l.next_token());
            CATCH_REQUIRE(string->is(csspp::node_type_t::STRING));
            CATCH_REQUIRE(string->get_string() == word);
            csspp::position const & npos(string->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // no error left over
        VERIFY_ERRORS("");
    }

    // one simple string with ' including "
    {
        std::stringstream ss;
        ss << "'This \"word\" sounds wrong!'";
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // string
        {
            csspp::node::pointer_t string(l.next_token());
            CATCH_REQUIRE(string->is(csspp::node_type_t::STRING));
            CATCH_REQUIRE(string->get_string() == "This \"word\" sounds wrong!");
            csspp::position const & npos(string->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // no error left over
        VERIFY_ERRORS("");
    }

    // string with escaped characters
    {
        for(csspp::wide_char_t i(0); i < 0x110000; ++i)
        {
            switch(i)
            {
            case '\0':
            case ' ':
            case '\t':
            case '\n':
            case '\r':
            case '\f':
            case 0xFFFD:
            case 0x00FFFE:
            case 0x00FFFF:
            case 0x01FFFE:
            case 0x01FFFF:
            case 0x02FFFE:
            case 0x02FFFF:
            case 0x03FFFE:
            case 0x03FFFF:
            case 0x04FFFE:
            case 0x04FFFF:
            case 0x05FFFE:
            case 0x05FFFF:
            case 0x06FFFE:
            case 0x06FFFF:
            case 0x07FFFE:
            case 0x07FFFF:
            case 0x08FFFE:
            case 0x08FFFF:
            case 0x09FFFE:
            case 0x09FFFF:
            case 0x0AFFFE:
            case 0x0AFFFF:
            case 0x0BFFFE:
            case 0x0BFFFF:
            case 0x0CFFFE:
            case 0x0CFFFF:
            case 0x0DFFFE:
            case 0x0DFFFF:
            case 0x0EFFFE:
            case 0x0EFFFF:
            case 0x0FFFFE:
            case 0x0FFFFF:
            case 0x10FFFE:
            case 0x10FFFF:
                // skip on characters that are either invalid or generate
                // a "problem" (i.e. spaces get trimmed)
                continue;

            default:
                if(i >= 0xD800 &&  i <= 0xDFFF)
                {
                    continue;
                }
                break;

            }

            // note that we test with sensible characters first

            std::stringstream ss;
            if(rand() % 1 == 0)
            {
                // make sure to also test uppercase once in a while
                ss << std::uppercase;
            }
            ss << "'escape character #" << std::dec << i
               << " as: \\" << std::hex << i
               << " to see whether it works'";
            csspp::position pos("test.css");
            csspp::lexer l(ss, pos);

            // string
            {
                csspp::node::pointer_t string(l.next_token());
                CATCH_REQUIRE(string->is(csspp::node_type_t::STRING));
                std::stringstream out;
                out << "escape character #" << std::dec << i
                    << " as: " << l.wctomb(i)
                    << (i < 0x100000 ? "" : " ") // space gets eaten if less than 6 characters in escape sequence
                    << "to see whether it works";
                CATCH_REQUIRE(string->get_string() == out.str());
                csspp::position const & npos(string->get_position());
                CATCH_REQUIRE(npos.get_filename() == "test.css");
                CATCH_REQUIRE(npos.get_page() == 1);
                CATCH_REQUIRE(npos.get_line() == 1);
                CATCH_REQUIRE(npos.get_total_line() == 1);
            }

            CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

            // no error left over
            VERIFY_ERRORS("");
        }
    }

    // unterminated string before EOF
    {
        std::stringstream ss;
        ss << "'No terminator";
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // string
        {
            csspp::node::pointer_t string(l.next_token());
            CATCH_REQUIRE(string->is(csspp::node_type_t::STRING));
            CATCH_REQUIRE(string->get_string() == "No terminator");
            csspp::position const & npos(string->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);

            VERIFY_ERRORS("test.css(1): error: found an unterminated string.\n");
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // no error left over
        VERIFY_ERRORS("");
    }

    // unterminated string before \n
    {
        std::stringstream ss;
        ss << "'No terminator\nto that string";
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // string
        {
            csspp::node::pointer_t string(l.next_token());
            CATCH_REQUIRE(string->is(csspp::node_type_t::STRING));
            CATCH_REQUIRE(string->get_string() == "No terminator");
            csspp::position const & npos(string->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);

            VERIFY_ERRORS("test.css(1): error: found an unterminated string with an unescaped newline.\n");
        }

        // whitespace
        {
            csspp::node::pointer_t string(l.next_token());
            CATCH_REQUIRE(string->is(csspp::node_type_t::WHITESPACE));
            csspp::position const & npos(string->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 2);
            CATCH_REQUIRE(npos.get_total_line() == 2);
        }

        // identifier
        {
            csspp::node::pointer_t string(l.next_token());
            CATCH_REQUIRE(string->is(csspp::node_type_t::IDENTIFIER));
            CATCH_REQUIRE(string->get_string() == "to");
            csspp::position const & npos(string->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 2);
            CATCH_REQUIRE(npos.get_total_line() == 2);
        }

        // whitespace
        {
            csspp::node::pointer_t string(l.next_token());
            CATCH_REQUIRE(string->is(csspp::node_type_t::WHITESPACE));
            csspp::position const & npos(string->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 2);
            CATCH_REQUIRE(npos.get_total_line() == 2);
        }

        // identifier
        {
            csspp::node::pointer_t string(l.next_token());
            CATCH_REQUIRE(string->is(csspp::node_type_t::IDENTIFIER));
            CATCH_REQUIRE(string->get_string() == "that");
            csspp::position const & npos(string->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 2);
            CATCH_REQUIRE(npos.get_total_line() == 2);
        }

        // whitespace
        {
            csspp::node::pointer_t string(l.next_token());
            CATCH_REQUIRE(string->is(csspp::node_type_t::WHITESPACE));
            csspp::position const & npos(string->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 2);
            CATCH_REQUIRE(npos.get_total_line() == 2);
        }

        // identifier
        {
            csspp::node::pointer_t string(l.next_token());
            CATCH_REQUIRE(string->is(csspp::node_type_t::IDENTIFIER));
            CATCH_REQUIRE(string->get_string() == "string");
            csspp::position const & npos(string->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 2);
            CATCH_REQUIRE(npos.get_total_line() == 2);
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // no error left over
        VERIFY_ERRORS("");
    }

    // special escapes in a string: \ + <EOF>
    // (same as just EOF above: unterminated string)
    {
        std::stringstream ss;
        ss << "'No terminator\\";
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // string
        {
            csspp::node::pointer_t string(l.next_token());
            CATCH_REQUIRE(string->is(csspp::node_type_t::STRING));
            CATCH_REQUIRE(string->get_string() == "No terminator");
            csspp::position const & npos(string->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);

            VERIFY_ERRORS("test.css(1): error: found an unterminated string.\n");
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // no error left over
        VERIFY_ERRORS("");
    }

    // special escapes in a string: \ + '\n'
    // (this is actually legal!)
    {
        std::stringstream ss;
        ss << "'Line ncontinues on\\\nthe next line.'";
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // string
        {
            csspp::node::pointer_t string(l.next_token());
            CATCH_REQUIRE(string->is(csspp::node_type_t::STRING));
            CATCH_REQUIRE(string->get_string() == "Line ncontinues on\nthe next line.");
            csspp::position const & npos(string->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // no error left over
        VERIFY_ERRORS("");
    }

    // special escapes in a string: \ + <FFFD>
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << "'Bad Escape \\" << l.wctomb(0xFFFD) << " String'";

        // string
        {
            csspp::node::pointer_t string(l.next_token());
            CATCH_REQUIRE(string->is(csspp::node_type_t::STRING));
            CATCH_REQUIRE(string->get_string() == "Bad Escape  String");
            csspp::position const & npos(string->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);

            VERIFY_ERRORS("test.css(1): error: invalid character after a \\ character.\n");
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // no error left over
        VERIFY_ERRORS("");
    }

    // escapes in a string: \ + <number too large>
    for(int i(0x110000); i < 0x01000000; i += rand() % 1000 + 1)
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << "'Bad Escape \\" << std::hex << i << " String'";

        // string
        {
            csspp::node::pointer_t string(l.next_token());
            CATCH_REQUIRE(string->is(csspp::node_type_t::STRING));
            CATCH_REQUIRE(string->get_string() == "Bad Escape  String");
            csspp::position const & npos(string->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);

            VERIFY_ERRORS("test.css(1): error: escape character too large for Unicode.\n");
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // no error left over
        VERIFY_ERRORS("");
    }
}

CATCH_TEST_CASE("Identifiers", "[lexer] [identifier]")
{
    // a few simple identifiers
    for(int count(0); count < 10; ++count)
    {
        std::stringstream ss;
        size_t const len(rand() % 20 + 20);
        std::string word;
        for(size_t i(0); i < len; ++i)
        {
            // simple ascii letters
            int const c(rand() % 26 + 'a');
            ss << static_cast<char>(c);
            word += static_cast<char>(c);
        }
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // identifier
        {
            csspp::node::pointer_t string(l.next_token());
            CATCH_REQUIRE(string->is(csspp::node_type_t::IDENTIFIER));
            CATCH_REQUIRE(string->get_string() == word);
            csspp::position const & npos(string->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
    }

    // a few simple identifiers starting with '_'
    for(int count(0); count < 10; ++count)
    {
        std::stringstream ss;
        size_t const len(rand() % 20 + 20);
        std::string word("_");
        ss << '_';
        for(size_t i(0); i < len; ++i)
        {
            // simple ascii letters
            int const c(rand() % 26 + 'a');
            ss << static_cast<char>(c);
            word += static_cast<char>(c);
        }
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // identifier
        {
            csspp::node::pointer_t string(l.next_token());
            CATCH_REQUIRE(string->is(csspp::node_type_t::IDENTIFIER));
            CATCH_REQUIRE(string->get_string() == word);
            csspp::position const & npos(string->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
    }

    // a few simple identifiers starting with '@'
    for(int count(0); count < 10; ++count)
    {
        std::stringstream ss;
        size_t const len(rand() % 20 + 20);
        std::string word;
        ss << '@';
        for(size_t i(0); i < len; ++i)
        {
            // simple ascii letters
            int const c(rand() % 26 + 'a');
            ss << static_cast<char>(c);
            word += static_cast<char>(c);
        }
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // identifier
        {
            csspp::node::pointer_t string(l.next_token());
            CATCH_REQUIRE(string->is(csspp::node_type_t::AT_KEYWORD));
            CATCH_REQUIRE(string->get_string() == word);
            csspp::position const & npos(string->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
    }

    // try with empty '@' characters (i.e. invalid identifiers)
    {
        std::stringstream ss;
        ss << "@*@";
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // prefix-match
        {
            csspp::node::pointer_t string(l.next_token());
            CATCH_REQUIRE(string->is(csspp::node_type_t::MULTIPLY));
            csspp::position const & npos(string->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);

            VERIFY_ERRORS("test.css(1): error: found an empty identifier.\n");
        }

        // EOF
        {
            csspp::node::pointer_t eof(l.next_token());
            CATCH_REQUIRE(eof->is(csspp::node_type_t::EOF_TOKEN));

            VERIFY_ERRORS("test.css(1): error: found an empty identifier.\n");
        }
    }

    // identifiers starting with '-'
    for(int count(0); count < 10; ++count)
    {
        std::stringstream ss;
        size_t const len(rand() % 20 + 20);
        std::string word;
        word += '-';
        ss << '-';
        for(size_t i(0); i < len; ++i)
        {
            // simple ascii letters
            int const c(rand() % 26 + 'a');
            ss << static_cast<char>(c);
            word += static_cast<char>(c);
        }
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // identifier
        {
            csspp::node::pointer_t string(l.next_token());
            CATCH_REQUIRE(string->is(csspp::node_type_t::IDENTIFIER));
            CATCH_REQUIRE(string->get_string() == word);
            csspp::position const & npos(string->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
    }

    // identifiers cannot start with '--'
    {
        std::stringstream ss;
        ss << "--not-double-dash\n-\\-double-dash\n-\\----quintuple-dash-----\r\n";
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // subtract
        {
            csspp::node::pointer_t string(l.next_token());
            CATCH_REQUIRE(string->is(csspp::node_type_t::SUBTRACT));
            csspp::position const & npos(string->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // identifier
        {
            csspp::node::pointer_t string(l.next_token());
            CATCH_REQUIRE(string->is(csspp::node_type_t::IDENTIFIER));
            CATCH_REQUIRE(string->get_string() == "-not-double-dash");
            csspp::position const & npos(string->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // whitespace
        {
            csspp::node::pointer_t whitespace(l.next_token());
            CATCH_REQUIRE(whitespace->is(csspp::node_type_t::WHITESPACE));
            csspp::position const & npos(whitespace->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 2);
            CATCH_REQUIRE(npos.get_total_line() == 2);
        }

        // identifier
        {
            csspp::node::pointer_t string(l.next_token());
            CATCH_REQUIRE(string->is(csspp::node_type_t::IDENTIFIER));
            CATCH_REQUIRE(string->get_string() == "--double-dash");
            csspp::position const & npos(string->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 2);
            CATCH_REQUIRE(npos.get_total_line() == 2);
        }

        // whitespace
        {
            csspp::node::pointer_t whitespace(l.next_token());
            CATCH_REQUIRE(whitespace->is(csspp::node_type_t::WHITESPACE));
            csspp::position const & npos(whitespace->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 3);
            CATCH_REQUIRE(npos.get_total_line() == 3);
        }

        // identifier
        {
            csspp::node::pointer_t string(l.next_token());
            CATCH_REQUIRE(string->is(csspp::node_type_t::IDENTIFIER));
            CATCH_REQUIRE(string->get_string() == "-----quintuple-dash-----");
            csspp::position const & npos(string->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 3);
            CATCH_REQUIRE(npos.get_total_line() == 3);
        }

        // whitespace
        {
            csspp::node::pointer_t whitespace(l.next_token());
            CATCH_REQUIRE(whitespace->is(csspp::node_type_t::WHITESPACE));
            csspp::position const & npos(whitespace->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 4);
            CATCH_REQUIRE(npos.get_total_line() == 4);
        }

        // EOF
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
    }

    // identifiers starting with an escape (\)
    {
        std::stringstream ss;
        ss << "\\41lexis\n"
           << "\\42 abar\n"
           << "\\000043arlos\n";
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // identifier
        {
            csspp::node::pointer_t string(l.next_token());
            CATCH_REQUIRE(string->is(csspp::node_type_t::IDENTIFIER));
            CATCH_REQUIRE(string->get_string() == "Alexis"); // identifiers are forced to lowercase since they are case insensitive
            csspp::position const & npos(string->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // whitespace
        {
            csspp::node::pointer_t whitespace(l.next_token());
            CATCH_REQUIRE(whitespace->is(csspp::node_type_t::WHITESPACE));
            csspp::position const & npos(whitespace->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 2);
            CATCH_REQUIRE(npos.get_total_line() == 2);
        }

        // identifier
        {
            csspp::node::pointer_t string(l.next_token());
            CATCH_REQUIRE(string->is(csspp::node_type_t::IDENTIFIER));
            CATCH_REQUIRE(string->get_string() == "Babar"); // prove the space is eaten as expected
            csspp::position const & npos(string->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 2);
            CATCH_REQUIRE(npos.get_total_line() == 2);
        }

        // whitespace
        {
            csspp::node::pointer_t whitespace(l.next_token());
            CATCH_REQUIRE(whitespace->is(csspp::node_type_t::WHITESPACE));
            csspp::position const & npos(whitespace->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 3);
            CATCH_REQUIRE(npos.get_total_line() == 3);
        }

        // identifier
        {
            csspp::node::pointer_t string(l.next_token());
            CATCH_REQUIRE(string->is(csspp::node_type_t::IDENTIFIER));
            CATCH_REQUIRE(string->get_string() == "Carlos"); // prove the space is not required with 6 digits
            csspp::position const & npos(string->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 3);
            CATCH_REQUIRE(npos.get_total_line() == 3);
        }

        // whitespace
        {
            csspp::node::pointer_t whitespace(l.next_token());
            CATCH_REQUIRE(whitespace->is(csspp::node_type_t::WHITESPACE));
            csspp::position const & npos(whitespace->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 4);
            CATCH_REQUIRE(npos.get_total_line() == 4);
        }

        // EOF
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
    }

    // identifier with an empty escape (\ + <EOF>)
    {
        std::stringstream ss;
        ss << "This\\";
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // identifier
        {
            csspp::node::pointer_t string(l.next_token());
            CATCH_REQUIRE(string->is(csspp::node_type_t::IDENTIFIER));
            CATCH_REQUIRE(string->get_string() == "This");
            csspp::position const & npos(string->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);

            VERIFY_ERRORS("test.css(1): error: found EOF right after \\.\n");
        }

        // EOF
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
    }

    // empty identifier with an empty escape (\ + <EOF>)
    {
        std::stringstream ss;
        ss << "This \\";
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // identifier
        {
            csspp::node::pointer_t string(l.next_token());
            CATCH_REQUIRE(string->is(csspp::node_type_t::IDENTIFIER));
            CATCH_REQUIRE(string->get_string() == "This");
            csspp::position const & npos(string->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // whitespace
        {
            csspp::node::pointer_t whitespace(l.next_token());
            CATCH_REQUIRE(whitespace->is(csspp::node_type_t::WHITESPACE));
            csspp::position const & npos(whitespace->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // EOF
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        VERIFY_ERRORS(
                "test.css(1): error: found EOF right after \\.\n"
                "test.css(1): error: found an empty identifier.\n"
            );
    }

    // identifier with an escape followed by a newline (\ + <new-line>)
    {
        std::stringstream ss;
        ss << "This\\\nThat";
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // identifier
        {
            csspp::node::pointer_t string(l.next_token());
            CATCH_REQUIRE(string->is(csspp::node_type_t::IDENTIFIER));
            CATCH_REQUIRE(string->get_string() == "This");
            csspp::position const & npos(string->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);

            VERIFY_ERRORS(
                    "test.css(1): error: spurious newline character after a \\ character outside of a string.\n"
                );
        }

        // whitespace -- this one gets lost and we do not care much
        //{
        //    csspp::node::pointer_t whitespace(l.next_token());
        //    CATCH_REQUIRE(whitespace->is(csspp::node_type_t::WHITESPACE));
        //    csspp::position const & npos(whitespace->get_position());
        //    CATCH_REQUIRE(npos.get_filename() == "test.css");
        //    CATCH_REQUIRE(npos.get_page() == 1);
        //    CATCH_REQUIRE(npos.get_line() == 1);
        //    CATCH_REQUIRE(npos.get_total_line() == 1);
        //}

        // identifier
        {
            csspp::node::pointer_t string(l.next_token());
            CATCH_REQUIRE(string->is(csspp::node_type_t::IDENTIFIER));
            CATCH_REQUIRE(string->get_string() == "That");
            csspp::position const & npos(string->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 2);
            CATCH_REQUIRE(npos.get_total_line() == 2);
        }

        // EOF
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
    }

    // identifiers written between parenthesis and "don't do that"
    for(int count(0); count < 10; ++count)
    {
        std::stringstream ss;
        size_t const len(rand() % 20 + 20);
        std::string word;
        word += "(";
        ss << "\\(";
        for(size_t i(0); i < len; ++i)
        {
            // simple ascii letters
            int const c(rand() % 26 + 'a');
            if(c > 'f' && rand() % 5 == 0)
            {
                // add some random escape characters
                ss << "\\";
            }
            ss << static_cast<char>(c);
            word += static_cast<char>(c);
        }
        word += ")";
        ss << "\\)\n\\\"don\\'t\\ do\\ that\\\"";
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // identifier
        {
            csspp::node::pointer_t string(l.next_token());
            CATCH_REQUIRE(string->is(csspp::node_type_t::IDENTIFIER));
            CATCH_REQUIRE(string->get_string() == word);
            csspp::position const & npos(string->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // whitespace
        {
            csspp::node::pointer_t whitespace(l.next_token());
            CATCH_REQUIRE(whitespace->is(csspp::node_type_t::WHITESPACE));
            csspp::position const & npos(whitespace->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 2);
            CATCH_REQUIRE(npos.get_total_line() == 2);
        }

        // identifier
        {
            csspp::node::pointer_t string(l.next_token());
            CATCH_REQUIRE(string->is(csspp::node_type_t::IDENTIFIER));
            CATCH_REQUIRE(string->get_string() == "\"don't do that\""); // yes, it's possible
            csspp::position const & npos(string->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 2);
            CATCH_REQUIRE(npos.get_total_line() == 2);
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
    }

    // identifier with an escape followed by 0xFFFD (\ + <FFFD>)
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << "This\\" << l.wctomb(0xFFFD) << "\\ ID";

        // identifier
        {
            csspp::node::pointer_t string(l.next_token());
            CATCH_REQUIRE(string->is(csspp::node_type_t::IDENTIFIER));
            CATCH_REQUIRE(string->get_string() == "This");
            csspp::position const & npos(string->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);

            VERIFY_ERRORS("test.css(1): error: invalid character after a \\ character.\n");
        }

        // identifier
        {
            csspp::node::pointer_t string(l.next_token());
            CATCH_REQUIRE(string->is(csspp::node_type_t::IDENTIFIER));
            CATCH_REQUIRE(string->get_string() == " ID");
            csspp::position const & npos(string->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // EOF
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
    }

    // identifier with an escape followed by "0" (\0)
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << "This\\0ID";

        // identifier
        {
            csspp::node::pointer_t string(l.next_token());
            CATCH_REQUIRE(string->is(csspp::node_type_t::IDENTIFIER));
            CATCH_REQUIRE(string->get_string() == "This");
            csspp::position const & npos(string->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);

            VERIFY_ERRORS("test.css(1): error: escape character '\\0' is not acceptable in CSS.\n");
        }

        // identifier
        {
            csspp::node::pointer_t string(l.next_token());
            CATCH_REQUIRE(string->is(csspp::node_type_t::IDENTIFIER));
            CATCH_REQUIRE(string->get_string() == "ID");
            csspp::position const & npos(string->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // EOF
        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Urls", "[lexer] [identifier] [url] [function]")
{
    // a few simple URLs
    CATCH_START_SECTION("simple URLs")
    {
        for(int count(0); count < 10; ++count)
        {
            std::stringstream ss;
            size_t const len(rand() % 20 + 20);
            std::string word;
            ss << "url(";
            size_t const leading(count == 0 ? 0 : rand() % 10);
            for(size_t i(0); i < leading; ++i)
            {
                ss << ' ';
            }
            for(size_t i(0); i < len; ++i)
            {
                // simple ascii letters
                int const c(rand() % 26 + 'a');
                ss << static_cast<char>(c);
                word += static_cast<char>(c);
            }
            size_t const trailing(count == 9 ? 0 : rand() % 10);
            for(size_t i(0); i < trailing; ++i)
            {
                ss << ' ';
            }
            ss << ")";
            csspp::position pos("test.css");
            csspp::lexer l(ss, pos);

            // url
            {
                csspp::node::pointer_t string(l.next_token());
                CATCH_REQUIRE(string->is(csspp::node_type_t::URL));
                CATCH_REQUIRE(string->get_string() == word);
                csspp::position const & npos(string->get_position());
                CATCH_REQUIRE(npos.get_filename() == "test.css");
                CATCH_REQUIRE(npos.get_page() == 1);
                CATCH_REQUIRE(npos.get_line() == 1);
                CATCH_REQUIRE(npos.get_total_line() == 1);
            }

            CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
        }

        VERIFY_ERRORS("");
    }
    CATCH_END_SECTION()

    // a few simple quoted URLs
    CATCH_START_SECTION("simple quoted URLs")
    {
        for(int count(0); count < 10; ++count)
        {
            std::stringstream ss;
            size_t const len(rand() % 20 + 20);
            std::string word;
            ss << "url(";
            size_t const leading(rand() % 10);
            for(size_t i(0); i < leading; ++i)
            {
                ss << ' ';
            }
            char const quote("\"'"[rand() % 2]);
            ss << quote;
            for(size_t i(0); i < len; ++i)
            {
                // simple ascii letters
                int const c(rand() % 26 + 'a');
                ss << static_cast<char>(c);
                word += static_cast<char>(c);
            }
            ss << quote;
            size_t const trailing(rand() % 10);
            for(size_t i(0); i < trailing; ++i)
            {
                ss << ' ';
            }
            ss << ")";
            csspp::position pos("test.css");
            csspp::lexer l(ss, pos);

            // url
            {
                csspp::node::pointer_t string(l.next_token());
                CATCH_REQUIRE(string->is(csspp::node_type_t::URL));
                CATCH_REQUIRE(string->get_string() == word);
                csspp::position const & npos(string->get_position());
                CATCH_REQUIRE(npos.get_filename() == "test.css");
                CATCH_REQUIRE(npos.get_page() == 1);
                CATCH_REQUIRE(npos.get_line() == 1);
                CATCH_REQUIRE(npos.get_total_line() == 1);
            }

            CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
        }

        VERIFY_ERRORS("");
    }
    CATCH_END_SECTION()

    // an invalid URL with EOF too soon
    CATCH_START_SECTION("invalid URL with EOF too soon")
    {
        for(int count(0); count < 10; ++count)
        {
            std::stringstream ss;
            size_t const len(rand() % 20 + 20);
            std::string word;
            ss << "url(";
            size_t const leading(count == 0 ? 0 : rand() % 10);
            for(size_t i(0); i < leading; ++i)
            {
                ss << ' ';
            }
            for(size_t i(0); i < len; ++i)
            {
                // simple ascii letters
                int const c(rand() % 26 + 'a');
                ss << static_cast<char>(c);
                word += static_cast<char>(c);
            }
            csspp::position pos("test.css");
            csspp::lexer l(ss, pos);

            // url
            {
                csspp::node::pointer_t string(l.next_token());
                CATCH_REQUIRE(string->is(csspp::node_type_t::URL));
                CATCH_REQUIRE(string->get_string() == word);
                csspp::position const & npos(string->get_position());
                CATCH_REQUIRE(npos.get_filename() == "test.css");
                CATCH_REQUIRE(npos.get_page() == 1);
                CATCH_REQUIRE(npos.get_line() == 1);
                CATCH_REQUIRE(npos.get_total_line() == 1);

                VERIFY_ERRORS(
                        "test.css(1): error: found an invalid URL, one with forbidden characters.\n"
                    );
            }

            CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
        }

        VERIFY_ERRORS("");
    }
    CATCH_END_SECTION()

    // an invalid URL with '"', "'", '(', and non-printable
    CATCH_START_SECTION("invalid URL with various unacceptable characters")
    {
        char const invalid_chars[] = "\"'(\x8\xb\xe\xf\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f\x7f";
        for(size_t ic(0); ic < sizeof(invalid_chars) / sizeof(invalid_chars[0]); ++ic)
        {
            for(int count(0); count < 10; ++count)
            {
                std::stringstream ss;
                csspp::position pos("test.css");
                csspp::lexer l(ss, pos);
                std::string word;
                ss << "url(";
                size_t const leading(count == 0 ? 0 : rand() % 10);
                for(size_t i(0); i < leading; ++i)
                {
                    ss << ' ';
                }
                size_t const len(rand() % 20 + 20);
                for(size_t i(0); i < len; ++i)
                {
                    // simple ascii letters
                    int const c(rand() % 26 + 'a');
                    ss << static_cast<char>(c);
                    word += static_cast<char>(c);
                }
                size_t const trailing(count & 1 ? 0 : rand() % 10);
                for(size_t i(0); i < trailing; ++i)
                {
                    ss << ' ';
                }
                if(invalid_chars[ic])
                {
                    ss << invalid_chars[ic];
                }
                else
                {
                    // we reached the NULL, insert 0xFFFD instead
                    ss << l.wctomb(0xFFFD);
                }

                // url
                {
                    csspp::node::pointer_t string(l.next_token());
                    CATCH_REQUIRE(string->is(csspp::node_type_t::URL));
                    CATCH_REQUIRE(string->get_string() == word);
                    csspp::position const & npos(string->get_position());
                    CATCH_REQUIRE(npos.get_filename() == "test.css");
                    CATCH_REQUIRE(npos.get_page() == 1);
                    CATCH_REQUIRE(npos.get_line() == 1);
                    CATCH_REQUIRE(npos.get_total_line() == 1);

                    VERIFY_ERRORS(
                            trailing == 0
                                ? "test.css(1): error: found an invalid URL, one with forbidden characters.\n"
                                : "test.css(1): error: found an invalid URL, one which includes spaces or has a missing ')'.\n"
                        );
                }

                CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
            }
        }

        VERIFY_ERRORS("");
    }
    CATCH_END_SECTION()

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Functions", "[lexer] [identifier] [function]")
{
    // a few simple functions
    for(int count(0); count < 10; ++count)
    {
        std::stringstream ss;
        size_t const len(rand() % 20 + 20);
        std::string word;
        for(size_t i(0); i < len; ++i)
        {
            // simple ascii letters
            int const c(rand() % 26 + 'a');
            ss << static_cast<char>(c);
            word += static_cast<char>(c);
        }
        ss << "(123)";
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // function
        {
            csspp::node::pointer_t string(l.next_token());
            CATCH_REQUIRE(string->is(csspp::node_type_t::FUNCTION));
            CATCH_REQUIRE(string->get_string() == word);
            csspp::position const & npos(string->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // number
        {
            csspp::node::pointer_t string(l.next_token());
            CATCH_REQUIRE(string->is(csspp::node_type_t::INTEGER));
            CATCH_REQUIRE(string->get_integer() == 123);
            csspp::position const & npos(string->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // parenthesis
        {
            csspp::node::pointer_t string(l.next_token());
            CATCH_REQUIRE(string->is(csspp::node_type_t::CLOSE_PARENTHESIS));
            csspp::position const & npos(string->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Numbers", "[lexer] [number]")
{
    // a few simple integers
    for(int i(-10000); i <= 10000; ++i)
    {
        std::stringstream ss;
        ss << i;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // sign
        //if(i < 0)
        //{
        //    csspp::node::pointer_t integer(l.next_token());
        //    CATCH_REQUIRE(integer->is(csspp::node_type_t::SUBTRACT));
        //    csspp::position const & npos(integer->get_position());
        //    CATCH_REQUIRE(npos.get_filename() == "test.css");
        //    CATCH_REQUIRE(npos.get_page() == 1);
        //    CATCH_REQUIRE(npos.get_line() == 1);
        //    CATCH_REQUIRE(npos.get_total_line() == 1);
        //}

        // integer
        {
            csspp::node::pointer_t integer(l.next_token());
            CATCH_REQUIRE(integer->is(csspp::node_type_t::INTEGER));
            CATCH_REQUIRE(integer->get_integer() == i);
            csspp::position const & npos(integer->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
    }

    // a few simple numbers (decimal / decimal)
    // no exponent, but uses a sign
    for(int i(0); i < 1000000000; i += rand() % 10000 + 1)
    {
        std::stringstream ss;
        bool const negative(rand() % 1 == 0);
        char const *sign(negative ? "-" : (rand() % 5 == 0 ? "+" : ""));
        ss << sign << i / 1000;
        bool const floating_point(i % 1000 != 0);
        if(floating_point)
        {
            ss << "." << std::setw(3) << std::setfill('0') << i % 1000;
        }
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        //if(negative)
        //{
        //    csspp::node::pointer_t subtract(l.next_token());
        //    CATCH_REQUIRE(subtract->is(csspp::node_type_t::SUBTRACT));
        //    csspp::position const & npos(subtract->get_position());
        //    CATCH_REQUIRE(npos.get_filename() == "test.css");
        //    CATCH_REQUIRE(npos.get_page() == 1);
        //    CATCH_REQUIRE(npos.get_line() == 1);
        //    CATCH_REQUIRE(npos.get_total_line() == 1);
        //}
        //else if(*sign == '+')
        //{
        //    csspp::node::pointer_t subtract(l.next_token());
        //    CATCH_REQUIRE(subtract->is(csspp::node_type_t::ADD));
        //    csspp::position const & npos(subtract->get_position());
        //    CATCH_REQUIRE(npos.get_filename() == "test.css");
        //    CATCH_REQUIRE(npos.get_page() == 1);
        //    CATCH_REQUIRE(npos.get_line() == 1);
        //    CATCH_REQUIRE(npos.get_total_line() == 1);
        //}

        if(floating_point)
        {
            // decimal number
            csspp::node::pointer_t string(l.next_token());
            CATCH_REQUIRE(string->is(csspp::node_type_t::DECIMAL_NUMBER));
//std::cerr << "*** from [" << ss.str()
//          << "] or [" << static_cast<csspp::decimal_number_t>(i) / 1000.0
//          << "] we got [" << string->get_decimal_number()
//          << "] |" << fabs(string->get_decimal_number())
//          << "| (sign: " << (negative ? "-" : "+") << ")\n";
            CATCH_REQUIRE(fabs(fabs(string->get_decimal_number()) - static_cast<csspp::decimal_number_t>(i) / 1000.0) < 0.00001);
            if(negative)
            {
                CATCH_REQUIRE(string->get_decimal_number() <= 0.0);
            }
            else
            {
                CATCH_REQUIRE(string->get_decimal_number() >= 0.0);
            }
            csspp::position const & npos(string->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }
        else
        {
            // integer
            csspp::node::pointer_t string(l.next_token());
            CATCH_REQUIRE(string->is(csspp::node_type_t::INTEGER));
            CATCH_REQUIRE(string->get_integer() == (negative ? -1 : 1) * i / 1000);
            csspp::position const & npos(string->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
    }

    // a few simple decimals with an exponent
    for(int i(-10000); i <= 10000; ++i)
    {
        // numbers starting with a digit (-0.3e5)
        {
            std::stringstream ss;
            auto generate_sign = [](){
                    switch(rand() % 3)
                    {
                    case 0:
                        return "-";

                    case 1:
                        return "+";

                    default: // case 2:
                        return "";

                    }
                };
            char const *exponent_sign(generate_sign());
            int const e(rand() % 100);
            char const *sign(i < 0 ? "-" : (rand() % 5 == 0 ? "+" : ""));
            ss << sign
               << abs(i) / 1000
               << "." << std::setw(3) << std::setfill('0') << abs(i) % 1000
               << std::setw(0) << std::setfill('\0') << "e" << exponent_sign << e;
            csspp::position pos("test.css");
            csspp::lexer l(ss, pos);
            csspp::decimal_number_t our_number(static_cast<csspp::decimal_number_t>(i) / 1000.0 * pow(10.0, e * (strcmp(exponent_sign, "-") == 0 ? -1 : 1)));
//std::cerr << "*** from [" << ss.str()
//          << "] or [" << our_number
//          << "] sign & exponent [" << exponent_sign << "|" << e
//          << "] ... ";

            // sign
            //if(i < 0)
            //{
            //    csspp::node::pointer_t subtract(l.next_token());
            //    CATCH_REQUIRE(subtract->is(csspp::node_type_t::SUBTRACT));
            //    csspp::position const & npos(subtract->get_position());
            //    CATCH_REQUIRE(npos.get_filename() == "test.css");
            //    CATCH_REQUIRE(npos.get_page() == 1);
            //    CATCH_REQUIRE(npos.get_line() == 1);
            //    CATCH_REQUIRE(npos.get_total_line() == 1);
            //}
            //else if(*sign == '+')
            //{
            //    csspp::node::pointer_t subtract(l.next_token());
            //    CATCH_REQUIRE(subtract->is(csspp::node_type_t::ADD));
            //    csspp::position const & npos(subtract->get_position());
            //    CATCH_REQUIRE(npos.get_filename() == "test.css");
            //    CATCH_REQUIRE(npos.get_page() == 1);
            //    CATCH_REQUIRE(npos.get_line() == 1);
            //    CATCH_REQUIRE(npos.get_total_line() == 1);
            //}

            // decimal number
            {
                csspp::node::pointer_t decimal_number(l.next_token());
//std::cerr << "*** type is " << static_cast<int>(decimal_number->get_type()) << " ***\n";
                CATCH_REQUIRE(decimal_number->is(csspp::node_type_t::DECIMAL_NUMBER));
                csspp::decimal_number_t const result(fabs(decimal_number->get_decimal_number()));
                csspp::decimal_number_t const abs_number(fabs(our_number));
                csspp::decimal_number_t const delta(fabs(result - abs_number));
                csspp::decimal_number_t const diff(delta / pow(10.0, e * (strcmp(exponent_sign, "-") == 0 ? -1 : 1)));

//std::cerr << "we got [" << result
//          << "| vs |" << abs_number
//          << "| diff = " << diff
//          << "\n";
//csspp::decimal_number_t r(fabs(decimal_number->get_decimal_number()));
//csspp::decimal_number_t q(fabs(our_number));
//std::cerr << std::hex
//          << *reinterpret_cast<int64_t *>(&r) << "\n"
//          << *reinterpret_cast<int64_t *>(&q) << " " << (r - q) << " -> " << diff << "\n";
                CATCH_REQUIRE(diff < 0.00001);
                if(*sign == '-')
                {
                    CATCH_REQUIRE(decimal_number->get_decimal_number() <= 0);
                }
                else
                {
                    CATCH_REQUIRE(decimal_number->get_decimal_number() >= 0);
                }
                csspp::position const & npos(decimal_number->get_position());
                CATCH_REQUIRE(npos.get_filename() == "test.css");
                CATCH_REQUIRE(npos.get_page() == 1);
                CATCH_REQUIRE(npos.get_line() == 1);
                CATCH_REQUIRE(npos.get_total_line() == 1);
            }

            CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
        }

        // numbers starting with a period (.25e3)
        if(i >= -999 && i <= 999)
        {
            std::stringstream ss;
            auto generate_sign = [](){
                    switch(rand() % 3)
                    {
                    case 0:
                        return "-";

                    case 1:
                        return "+";

                    default: // case 2:
                        return "";

                    }
                };
            char const *exponent_sign(generate_sign());
            int const e(rand() % 100);
            char const *sign(i < 0 ? "-" : (rand() % 5 == 0 ? "+" : ""));
            ss << sign
               //<< abs(i) / 1000 -- this would always be '0' here
               << "." << std::setw(3) << std::setfill('0') << abs(i) % 1000
               << std::setw(0) << std::setfill('\0') << "e" << exponent_sign << e;
            csspp::position pos("test.css");
            csspp::lexer l(ss, pos);
            csspp::decimal_number_t our_number(static_cast<csspp::decimal_number_t>(i) / 1000.0 * pow(10.0, e * (strcmp(exponent_sign, "-") == 0 ? -1 : 1)));
//std::cerr << "*** from [" << ss.str()
//          << "] or [" << our_number
//          << "] sign & exponent [" << exponent_sign << "|" << e
//          << "] ... ";

            // sign
            //if(i < 0)
            //{
            //    csspp::node::pointer_t subtract(l.next_token());
            //    CATCH_REQUIRE(subtract->is(csspp::node_type_t::SUBTRACT));
            //    csspp::position const & npos(subtract->get_position());
            //    CATCH_REQUIRE(npos.get_filename() == "test.css");
            //    CATCH_REQUIRE(npos.get_page() == 1);
            //    CATCH_REQUIRE(npos.get_line() == 1);
            //    CATCH_REQUIRE(npos.get_total_line() == 1);
            //}
            //else if(*sign == '+')
            //{
            //    csspp::node::pointer_t subtract(l.next_token());
            //    CATCH_REQUIRE(subtract->is(csspp::node_type_t::ADD));
            //    csspp::position const & npos(subtract->get_position());
            //    CATCH_REQUIRE(npos.get_filename() == "test.css");
            //    CATCH_REQUIRE(npos.get_page() == 1);
            //    CATCH_REQUIRE(npos.get_line() == 1);
            //    CATCH_REQUIRE(npos.get_total_line() == 1);
            //}

            // decimal number
            {
                csspp::node::pointer_t decimal_number(l.next_token());
                CATCH_REQUIRE(decimal_number->is(csspp::node_type_t::DECIMAL_NUMBER));
                csspp::decimal_number_t const result(fabs(decimal_number->get_decimal_number()));
                csspp::decimal_number_t const abs_number(fabs(our_number));
                csspp::decimal_number_t const delta(fabs(result - abs_number));
                csspp::decimal_number_t const diff(delta / pow(10.0, e * (strcmp(exponent_sign, "-") == 0 ? -1 : 1)));

//std::cerr << "we got [" << result
//          << "| vs |" << abs_number
//          << "| diff = " << diff
//          << "\n";
//csspp::decimal_number_t r(fabs(decimal_number->get_decimal_number()));
//csspp::decimal_number_t q(fabs(our_number));
//std::cerr << std::hex
//          << *reinterpret_cast<int64_t *>(&r) << "\n"
//          << *reinterpret_cast<int64_t *>(&q) << " " << (r - q) << " -> " << diff << "\n";
                CATCH_REQUIRE(diff < 0.00001);
                if(*sign == '-')
                {
                    CATCH_REQUIRE(decimal_number->get_decimal_number() <= 0);
                }
                else
                {
                    CATCH_REQUIRE(decimal_number->get_decimal_number() >= 0);
                }
                csspp::position const & npos(decimal_number->get_position());
                CATCH_REQUIRE(npos.get_filename() == "test.css");
                CATCH_REQUIRE(npos.get_page() == 1);
                CATCH_REQUIRE(npos.get_line() == 1);
                CATCH_REQUIRE(npos.get_total_line() == 1);
            }

            CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
        }
    }

    // check maximum 64 bit number
    {
        std::stringstream ss;
        // largest 64 bit positive number is 9223372036854775807
        ss << "perfect: 9223372036854775807";
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // identifier
        {
            csspp::node::pointer_t identifier(l.next_token());
            CATCH_REQUIRE(identifier->is(csspp::node_type_t::IDENTIFIER));
            CATCH_REQUIRE(identifier->get_string() == "perfect");
            csspp::position const & npos(identifier->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // colon
        {
            csspp::node::pointer_t colon(l.next_token());
            CATCH_REQUIRE(colon->is(csspp::node_type_t::COLON));
            csspp::position const & npos(colon->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // whitespace
        {
            csspp::node::pointer_t whitespace(l.next_token());
            CATCH_REQUIRE(whitespace->is(csspp::node_type_t::WHITESPACE));
            csspp::position const & npos(whitespace->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // decimal number
        {
            csspp::node::pointer_t integer(l.next_token());
            CATCH_REQUIRE(integer->is(csspp::node_type_t::INTEGER));
            CATCH_REQUIRE(integer->get_integer() == 9223372036854775807LL);
            csspp::position const & npos(integer->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
    }

    // check a number so large that it fails
    {
        std::stringstream ss;
        // largest 64 bit positive number is 9223372036854775807
        ss << "too\\ large: 10000000000000000000";
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // identifier
        {
            csspp::node::pointer_t identifier(l.next_token());
            CATCH_REQUIRE(identifier->is(csspp::node_type_t::IDENTIFIER));
            CATCH_REQUIRE(identifier->get_string() == "too large");
            csspp::position const & npos(identifier->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // colon
        {
            csspp::node::pointer_t colon(l.next_token());
            CATCH_REQUIRE(colon->is(csspp::node_type_t::COLON));
            csspp::position const & npos(colon->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // whitespace
        {
            csspp::node::pointer_t whitespace(l.next_token());
            CATCH_REQUIRE(whitespace->is(csspp::node_type_t::WHITESPACE));
            csspp::position const & npos(whitespace->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // integer
        {
            csspp::node::pointer_t integer(l.next_token());
            CATCH_REQUIRE(integer->is(csspp::node_type_t::INTEGER));
            //CATCH_REQUIRE(integer->get_integer() == ???); -- there is an overflow so we decide not to replicate it here
            csspp::position const & npos(integer->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);

            VERIFY_ERRORS("test.css(1): error: integral part too large for a number.\n");
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
    }

    // check a decimal part that's too large
    // (we limit those to 8 digits at this time)
    {
        std::stringstream ss;
        ss << "decimal_part_too_long: 1.000000000000000000000";
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // identifier
        {
            csspp::node::pointer_t identifier(l.next_token());
            CATCH_REQUIRE(identifier->is(csspp::node_type_t::IDENTIFIER));
            CATCH_REQUIRE(identifier->get_string() == "decimal_part_too_long");
            csspp::position const & npos(identifier->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // colon
        {
            csspp::node::pointer_t colon(l.next_token());
            CATCH_REQUIRE(colon->is(csspp::node_type_t::COLON));
            csspp::position const & npos(colon->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // whitespace
        {
            csspp::node::pointer_t whitespace(l.next_token());
            CATCH_REQUIRE(whitespace->is(csspp::node_type_t::WHITESPACE));
            csspp::position const & npos(whitespace->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // decimal number
        {
            csspp::node::pointer_t decimal_number(l.next_token());
            CATCH_REQUIRE(decimal_number->is(csspp::node_type_t::DECIMAL_NUMBER));
            //CATCH_REQUIRE(decimal_number->get_decimal_number() == ???); -- there may be an overflow so we decide not to replicate it here
            csspp::position const & npos(decimal_number->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);

            VERIFY_ERRORS("test.css(1): error: fraction too large for a decimal number.\n");
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
    }

    // decimal number with no digits in the decimal fraction part is an error
    {
        std::stringstream ss;
        ss << "font-size: 154.;";
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // identifier
        {
            csspp::node::pointer_t identifier(l.next_token());
            CATCH_REQUIRE(identifier->is(csspp::node_type_t::IDENTIFIER));
            CATCH_REQUIRE(identifier->get_string() == "font-size");
            csspp::position const & npos(identifier->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // colon
        {
            csspp::node::pointer_t colon(l.next_token());
            CATCH_REQUIRE(colon->is(csspp::node_type_t::COLON));
            csspp::position const & npos(colon->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // whitespace
        {
            csspp::node::pointer_t whitespace(l.next_token());
            CATCH_REQUIRE(whitespace->is(csspp::node_type_t::WHITESPACE));
            csspp::position const & npos(whitespace->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // decimal number
        {
            csspp::node::pointer_t decimal_number(l.next_token());
            CATCH_REQUIRE(decimal_number->is(csspp::node_type_t::DECIMAL_NUMBER));
            CATCH_REQUIRE(decimal_number->get_decimal_number() == 154.0_a);
            csspp::position const & npos(decimal_number->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);

            VERIFY_ERRORS("test.css(1): error: decimal number must have at least one digit after the decimal point.\n");
        }

        // semi-colon
        {
            csspp::node::pointer_t whitespace(l.next_token());
            CATCH_REQUIRE(whitespace->is(csspp::node_type_t::SEMICOLON));
            csspp::position const & npos(whitespace->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
    }

    // try parsing an expression
    {
        std::stringstream ss;
        ss << "font-size: 154*3;";
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // identifier
        {
            csspp::node::pointer_t identifier(l.next_token());
            CATCH_REQUIRE(identifier->is(csspp::node_type_t::IDENTIFIER));
            CATCH_REQUIRE(identifier->get_string() == "font-size");
            csspp::position const & npos(identifier->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // colon
        {
            csspp::node::pointer_t colon(l.next_token());
            CATCH_REQUIRE(colon->is(csspp::node_type_t::COLON));
            csspp::position const & npos(colon->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // whitespace
        {
            csspp::node::pointer_t whitespace(l.next_token());
            CATCH_REQUIRE(whitespace->is(csspp::node_type_t::WHITESPACE));
            csspp::position const & npos(whitespace->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // integer
        {
            csspp::node::pointer_t integer(l.next_token());
            CATCH_REQUIRE(integer->is(csspp::node_type_t::INTEGER));
            CATCH_REQUIRE(integer->get_integer() == 154);
            csspp::position const & npos(integer->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // multiply
        {
            csspp::node::pointer_t integer(l.next_token());
            CATCH_REQUIRE(integer->is(csspp::node_type_t::MULTIPLY));
            csspp::position const & npos(integer->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // integer
        {
            csspp::node::pointer_t integer(l.next_token());
            CATCH_REQUIRE(integer->is(csspp::node_type_t::INTEGER));
            CATCH_REQUIRE(integer->get_integer() == 3);
            csspp::position const & npos(integer->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // semi-colon
        {
            csspp::node::pointer_t whitespace(l.next_token());
            CATCH_REQUIRE(whitespace->is(csspp::node_type_t::SEMICOLON));
            csspp::position const & npos(whitespace->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
    }

    // test with an exponent that's too large
    {
        std::stringstream ss;
        ss << "font-size: 154e-1024;";
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // identifier
        {
            csspp::node::pointer_t identifier(l.next_token());
            CATCH_REQUIRE(identifier->is(csspp::node_type_t::IDENTIFIER));
            CATCH_REQUIRE(identifier->get_string() == "font-size");
            csspp::position const & npos(identifier->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // colon
        {
            csspp::node::pointer_t colon(l.next_token());
            CATCH_REQUIRE(colon->is(csspp::node_type_t::COLON));
            csspp::position const & npos(colon->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // whitespace
        {
            csspp::node::pointer_t whitespace(l.next_token());
            CATCH_REQUIRE(whitespace->is(csspp::node_type_t::WHITESPACE));
            csspp::position const & npos(whitespace->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // decimal number
        {
            csspp::node::pointer_t decimal_number(l.next_token());
            CATCH_REQUIRE(decimal_number->is(csspp::node_type_t::DECIMAL_NUMBER));
            //CATCH_REQUIRE(decimal_number->get_decimal_number() == ...); -- this is not a valid number
            csspp::position const & npos(decimal_number->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);

            VERIFY_ERRORS("test.css(1): error: exponent too large for a decimal number.\n");
        }

        // semi-colon
        {
            csspp::node::pointer_t whitespace(l.next_token());
            CATCH_REQUIRE(whitespace->is(csspp::node_type_t::SEMICOLON));
            csspp::position const & npos(whitespace->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Dimensions", "[lexer] [number] [dimension] [identifier]")
{
    // well known dimensions with integers
    {
        char const *dimensions[] = {
            "em",       // character em
            "px",       // pixel
            "pt"        // point
        };
        for(int i(-10000); i <= 10000; ++i)
        {
            for(size_t j(0); j < sizeof(dimensions) / sizeof(dimensions[0]); ++j)
            {
                std::stringstream ss;
                ss << i << dimensions[j];
                // direct escape?
                if(dimensions[j][0] > 'f')
                {
                    ss << "," << i << "\\" << dimensions[j];
                }
                ss << "," << i << "\\" << std::hex << static_cast<int>(dimensions[j][0]) << " " << std::string(dimensions[j]).substr(1)
                   << "," << std::dec << i << " " << dimensions[j]; // prove it does not work when we have a space
                csspp::position pos("test.css");
                csspp::lexer l(ss, pos);

                // sign
                //if(i < 0)
                //{
                //    csspp::node::pointer_t integer(l.next_token());
                //    CATCH_REQUIRE(integer->is(csspp::node_type_t::SUBTRACT));
                //    csspp::position const & npos(integer->get_position());
                //    CATCH_REQUIRE(npos.get_filename() == "test.css");
                //    CATCH_REQUIRE(npos.get_page() == 1);
                //    CATCH_REQUIRE(npos.get_line() == 1);
                //    CATCH_REQUIRE(npos.get_total_line() == 1);
                //}

                // dimension
                {
                    // a dimension is an integer or a decimal number
                    // with a string expressing the dimension
                    csspp::node::pointer_t dimension(l.next_token());
                    CATCH_REQUIRE(dimension->is(csspp::node_type_t::INTEGER));
                    CATCH_REQUIRE(dimension->get_integer() == i);
                    CATCH_REQUIRE(dimension->get_string() == dimensions[j]);
                    csspp::position const & npos(dimension->get_position());
                    CATCH_REQUIRE(npos.get_filename() == "test.css");
                    CATCH_REQUIRE(npos.get_page() == 1);
                    CATCH_REQUIRE(npos.get_line() == 1);
                    CATCH_REQUIRE(npos.get_total_line() == 1);
                }

                // direct escape?
                if(dimensions[j][0] > 'f')
                {
                    // comma
                    {
                        csspp::node::pointer_t comma(l.next_token());
                        CATCH_REQUIRE(comma->is(csspp::node_type_t::COMMA));
                        csspp::position const & npos(comma->get_position());
                        CATCH_REQUIRE(npos.get_filename() == "test.css");
                        CATCH_REQUIRE(npos.get_page() == 1);
                        CATCH_REQUIRE(npos.get_line() == 1);
                        CATCH_REQUIRE(npos.get_total_line() == 1);
                    }

                    // sign
                    //if(i < 0)
                    //{
                    //    csspp::node::pointer_t integer(l.next_token());
                    //    CATCH_REQUIRE(integer->is(csspp::node_type_t::SUBTRACT));
                    //    csspp::position const & npos(integer->get_position());
                    //    CATCH_REQUIRE(npos.get_filename() == "test.css");
                    //    CATCH_REQUIRE(npos.get_page() == 1);
                    //    CATCH_REQUIRE(npos.get_line() == 1);
                    //    CATCH_REQUIRE(npos.get_total_line() == 1);
                    //}

                    // dimension
                    {
                        // a dimension is an integer or a decimal number
                        // with a string expressing the dimension
                        csspp::node::pointer_t dimension(l.next_token());
                        CATCH_REQUIRE(dimension->is(csspp::node_type_t::INTEGER));
                        CATCH_REQUIRE(dimension->get_integer() == i);
                        CATCH_REQUIRE(dimension->get_string() == dimensions[j]);
                        csspp::position const & npos(dimension->get_position());
                        CATCH_REQUIRE(npos.get_filename() == "test.css");
                        CATCH_REQUIRE(npos.get_page() == 1);
                        CATCH_REQUIRE(npos.get_line() == 1);
                        CATCH_REQUIRE(npos.get_total_line() == 1);
                    }
                }

                // comma
                {
                    csspp::node::pointer_t comma(l.next_token());
                    CATCH_REQUIRE(comma->is(csspp::node_type_t::COMMA));
                    csspp::position const & npos(comma->get_position());
                    CATCH_REQUIRE(npos.get_filename() == "test.css");
                    CATCH_REQUIRE(npos.get_page() == 1);
                    CATCH_REQUIRE(npos.get_line() == 1);
                    CATCH_REQUIRE(npos.get_total_line() == 1);
                }

                // sign
                //if(i < 0)
                //{
                //    csspp::node::pointer_t integer(l.next_token());
                //    CATCH_REQUIRE(integer->is(csspp::node_type_t::SUBTRACT));
                //    csspp::position const & npos(integer->get_position());
                //    CATCH_REQUIRE(npos.get_filename() == "test.css");
                //    CATCH_REQUIRE(npos.get_page() == 1);
                //    CATCH_REQUIRE(npos.get_line() == 1);
                //    CATCH_REQUIRE(npos.get_total_line() == 1);
                //}

                // dimension
                {
                    // a dimension is an integer or a decimal number
                    // with a string expressing the dimension
                    csspp::node::pointer_t dimension(l.next_token());
                    CATCH_REQUIRE(dimension->is(csspp::node_type_t::INTEGER));
                    CATCH_REQUIRE(dimension->get_integer() == i);
                    CATCH_REQUIRE(dimension->get_string() == dimensions[j]);
                    csspp::position const & npos(dimension->get_position());
                    CATCH_REQUIRE(npos.get_filename() == "test.css");
                    CATCH_REQUIRE(npos.get_page() == 1);
                    CATCH_REQUIRE(npos.get_line() == 1);
                    CATCH_REQUIRE(npos.get_total_line() == 1);
                }

                // comma
                {
                    csspp::node::pointer_t comma(l.next_token());
                    CATCH_REQUIRE(comma->is(csspp::node_type_t::COMMA));
                    csspp::position const & npos(comma->get_position());
                    CATCH_REQUIRE(npos.get_filename() == "test.css");
                    CATCH_REQUIRE(npos.get_page() == 1);
                    CATCH_REQUIRE(npos.get_line() == 1);
                    CATCH_REQUIRE(npos.get_total_line() == 1);
                }

                // sign
                //if(i < 0)
                //{
                //    csspp::node::pointer_t integer(l.next_token());
                //    CATCH_REQUIRE(integer->is(csspp::node_type_t::SUBTRACT));
                //    csspp::position const & npos(integer->get_position());
                //    CATCH_REQUIRE(npos.get_filename() == "test.css");
                //    CATCH_REQUIRE(npos.get_page() == 1);
                //    CATCH_REQUIRE(npos.get_line() == 1);
                //    CATCH_REQUIRE(npos.get_total_line() == 1);
                //}

                // integer (separated!)
                {
                    csspp::node::pointer_t integer(l.next_token());
                    CATCH_REQUIRE(integer->is(csspp::node_type_t::INTEGER));
                    CATCH_REQUIRE(integer->get_integer() == i);
                    CATCH_REQUIRE(integer->get_string() == "");
                    csspp::position const & npos(integer->get_position());
                    CATCH_REQUIRE(npos.get_filename() == "test.css");
                    CATCH_REQUIRE(npos.get_page() == 1);
                    CATCH_REQUIRE(npos.get_line() == 1);
                    CATCH_REQUIRE(npos.get_total_line() == 1);
                }

                // whitespace
                {
                    csspp::node::pointer_t whitespace(l.next_token());
                    CATCH_REQUIRE(whitespace->is(csspp::node_type_t::WHITESPACE));
                    csspp::position const & npos(whitespace->get_position());
                    CATCH_REQUIRE(npos.get_filename() == "test.css");
                    CATCH_REQUIRE(npos.get_page() == 1);
                    CATCH_REQUIRE(npos.get_line() == 1);
                    CATCH_REQUIRE(npos.get_total_line() == 1);
                }

                // "dimension" (as a separate identifier)
                {
                    // a dimension is an integer or a decimal number
                    // with a string expressing the dimension
                    csspp::node::pointer_t dimension(l.next_token());
                    CATCH_REQUIRE(dimension->is(csspp::node_type_t::IDENTIFIER));
                    CATCH_REQUIRE(dimension->get_string() == dimensions[j]);
                    csspp::position const & npos(dimension->get_position());
                    CATCH_REQUIRE(npos.get_filename() == "test.css");
                    CATCH_REQUIRE(npos.get_page() == 1);
                    CATCH_REQUIRE(npos.get_line() == 1);
                    CATCH_REQUIRE(npos.get_total_line() == 1);
                }
                CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
            }
        }
    }

    // well known dimensions with decimal numbers
    {
        char const *dimensions[] = {
            "em",       // character em
            "px",       // pixel
            "pt"        // point
        };
        for(int i(-1000); i <= 1000; ++i)
        {
            for(size_t j(0); j < sizeof(dimensions) / sizeof(dimensions[0]); ++j)
            {
                std::stringstream ss;
                ss << (i < 0 ? "-" : "") << abs(i) / 100 << "." << std::setw(2) << std::setfill('0') << abs(i % 100) << dimensions[j]
                   << "," << (i < 0 ? "-" : "") << abs(i) / 100 << "." << std::setw(2) << abs(i % 100) << " " << dimensions[j]; // prove it does not work when we have a space
                csspp::position pos("test.css");
                csspp::lexer l(ss, pos);

                // sign
                //if(i < 0)
                //{
                //    csspp::node::pointer_t integer(l.next_token());
                //    CATCH_REQUIRE(integer->is(csspp::node_type_t::SUBTRACT));
                //    csspp::position const & npos(integer->get_position());
                //    CATCH_REQUIRE(npos.get_filename() == "test.css");
                //    CATCH_REQUIRE(npos.get_page() == 1);
                //    CATCH_REQUIRE(npos.get_line() == 1);
                //    CATCH_REQUIRE(npos.get_total_line() == 1);
                //}

                // dimension
                {
                    // a dimension is an integer or a decimal number
                    // with a string expressing the dimension
                    csspp::node::pointer_t dimension(l.next_token());
                    CATCH_REQUIRE(dimension->is(csspp::node_type_t::DECIMAL_NUMBER));
                    CATCH_REQUIRE(fabs(dimension->get_decimal_number() - i / 100.0) < 0.00001);
                    CATCH_REQUIRE(dimension->get_string() == dimensions[j]);
                    csspp::position const & npos(dimension->get_position());
                    CATCH_REQUIRE(npos.get_filename() == "test.css");
                    CATCH_REQUIRE(npos.get_page() == 1);
                    CATCH_REQUIRE(npos.get_line() == 1);
                    CATCH_REQUIRE(npos.get_total_line() == 1);
                }

                // comma
                {
                    csspp::node::pointer_t comma(l.next_token());
                    CATCH_REQUIRE(comma->is(csspp::node_type_t::COMMA));
                    csspp::position const & npos(comma->get_position());
                    CATCH_REQUIRE(npos.get_filename() == "test.css");
                    CATCH_REQUIRE(npos.get_page() == 1);
                    CATCH_REQUIRE(npos.get_line() == 1);
                    CATCH_REQUIRE(npos.get_total_line() == 1);
                }

                // sign
                //if(i < 0)
                //{
                //    csspp::node::pointer_t integer(l.next_token());
                //    CATCH_REQUIRE(integer->is(csspp::node_type_t::SUBTRACT));
                //    csspp::position const & npos(integer->get_position());
                //    CATCH_REQUIRE(npos.get_filename() == "test.css");
                //    CATCH_REQUIRE(npos.get_page() == 1);
                //    CATCH_REQUIRE(npos.get_line() == 1);
                //    CATCH_REQUIRE(npos.get_total_line() == 1);
                //}

                // decimal number (separated!)
                {
                    csspp::node::pointer_t decimal_number(l.next_token());
                    CATCH_REQUIRE(decimal_number->is(csspp::node_type_t::DECIMAL_NUMBER));
                    CATCH_REQUIRE(fabs(decimal_number->get_decimal_number() - i / 100.0) < 0.00001);
                    CATCH_REQUIRE(decimal_number->get_string() == "");
                    csspp::position const & npos(decimal_number->get_position());
                    CATCH_REQUIRE(npos.get_filename() == "test.css");
                    CATCH_REQUIRE(npos.get_page() == 1);
                    CATCH_REQUIRE(npos.get_line() == 1);
                    CATCH_REQUIRE(npos.get_total_line() == 1);
                }

                // whitespace
                {
                    csspp::node::pointer_t whitespace(l.next_token());
                    CATCH_REQUIRE(whitespace->is(csspp::node_type_t::WHITESPACE));
                    csspp::position const & npos(whitespace->get_position());
                    CATCH_REQUIRE(npos.get_filename() == "test.css");
                    CATCH_REQUIRE(npos.get_page() == 1);
                    CATCH_REQUIRE(npos.get_line() == 1);
                    CATCH_REQUIRE(npos.get_total_line() == 1);
                }

                // "dimension" (as a separate identifier)
                {
                    // a dimension is an integer or a decimal number
                    // with a string expressing the dimension
                    csspp::node::pointer_t dimension(l.next_token());
                    CATCH_REQUIRE(dimension->is(csspp::node_type_t::IDENTIFIER));
                    CATCH_REQUIRE(dimension->get_string() == dimensions[j]);
                    csspp::position const & npos(dimension->get_position());
                    CATCH_REQUIRE(npos.get_filename() == "test.css");
                    CATCH_REQUIRE(npos.get_page() == 1);
                    CATCH_REQUIRE(npos.get_line() == 1);
                    CATCH_REQUIRE(npos.get_total_line() == 1);
                }
                CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
            }
        }
    }

    // decimal numbers that start with "." or "-."
    {
        char const *dimensions[] = {
            "em",       // character em
            "px",       // pixel
            "pt"        // point
        };
        for(int i(-99); i <= 99; ++i)
        {
            for(size_t j(0); j < sizeof(dimensions) / sizeof(dimensions[0]); ++j)
            {
                std::stringstream ss;
                ss << (i < 0 ? "-" : "") << "." << std::setw(2) << std::setfill('0') << abs(i) << dimensions[j]
                   << "," << (i < 0 ? "-" : "") << "." << std::setw(2) << abs(i) << " " << dimensions[j]; // prove it does not work when we have a space
                csspp::position pos("test.css");
                csspp::lexer l(ss, pos);

                // sign
                //if(i < 0)
                //{
                //    csspp::node::pointer_t integer(l.next_token());
                //    CATCH_REQUIRE(integer->is(csspp::node_type_t::SUBTRACT));
                //    csspp::position const & npos(integer->get_position());
                //    CATCH_REQUIRE(npos.get_filename() == "test.css");
                //    CATCH_REQUIRE(npos.get_page() == 1);
                //    CATCH_REQUIRE(npos.get_line() == 1);
                //    CATCH_REQUIRE(npos.get_total_line() == 1);
                //}

                // dimension
                {
                    // a dimension is an integer or a decimal number
                    // with a string expressing the dimension
                    csspp::node::pointer_t dimension(l.next_token());
                    CATCH_REQUIRE(dimension->is(csspp::node_type_t::DECIMAL_NUMBER));
                    CATCH_REQUIRE(fabs(dimension->get_decimal_number() - i / 100.0) < 0.00001);
                    CATCH_REQUIRE(dimension->get_string() == dimensions[j]);
                    csspp::position const & npos(dimension->get_position());
                    CATCH_REQUIRE(npos.get_filename() == "test.css");
                    CATCH_REQUIRE(npos.get_page() == 1);
                    CATCH_REQUIRE(npos.get_line() == 1);
                    CATCH_REQUIRE(npos.get_total_line() == 1);
                }

                // comma
                {
                    csspp::node::pointer_t comma(l.next_token());
                    CATCH_REQUIRE(comma->is(csspp::node_type_t::COMMA));
                    csspp::position const & npos(comma->get_position());
                    CATCH_REQUIRE(npos.get_filename() == "test.css");
                    CATCH_REQUIRE(npos.get_page() == 1);
                    CATCH_REQUIRE(npos.get_line() == 1);
                    CATCH_REQUIRE(npos.get_total_line() == 1);
                }

                // sign
                //if(i < 0)
                //{
                //    csspp::node::pointer_t integer(l.next_token());
                //    CATCH_REQUIRE(integer->is(csspp::node_type_t::SUBTRACT));
                //    csspp::position const & npos(integer->get_position());
                //    CATCH_REQUIRE(npos.get_filename() == "test.css");
                //    CATCH_REQUIRE(npos.get_page() == 1);
                //    CATCH_REQUIRE(npos.get_line() == 1);
                //    CATCH_REQUIRE(npos.get_total_line() == 1);
                //}

                // decimal number (separated!)
                {
                    csspp::node::pointer_t decimal_number(l.next_token());
                    CATCH_REQUIRE(decimal_number->is(csspp::node_type_t::DECIMAL_NUMBER));
                    CATCH_REQUIRE(fabs(decimal_number->get_decimal_number() - i / 100.0) < 0.00001);
                    CATCH_REQUIRE(decimal_number->get_string() == "");
                    csspp::position const & npos(decimal_number->get_position());
                    CATCH_REQUIRE(npos.get_filename() == "test.css");
                    CATCH_REQUIRE(npos.get_page() == 1);
                    CATCH_REQUIRE(npos.get_line() == 1);
                    CATCH_REQUIRE(npos.get_total_line() == 1);
                }

                // whitespace
                {
                    csspp::node::pointer_t whitespace(l.next_token());
                    CATCH_REQUIRE(whitespace->is(csspp::node_type_t::WHITESPACE));
                    csspp::position const & npos(whitespace->get_position());
                    CATCH_REQUIRE(npos.get_filename() == "test.css");
                    CATCH_REQUIRE(npos.get_page() == 1);
                    CATCH_REQUIRE(npos.get_line() == 1);
                    CATCH_REQUIRE(npos.get_total_line() == 1);
                }

                // "dimension" (as a separate identifier)
                {
                    // a dimension is an integer or a decimal number
                    // with a string expressing the dimension
                    csspp::node::pointer_t dimension(l.next_token());
                    CATCH_REQUIRE(dimension->is(csspp::node_type_t::IDENTIFIER));
                    CATCH_REQUIRE(dimension->get_string() == dimensions[j]);
                    csspp::position const & npos(dimension->get_position());
                    CATCH_REQUIRE(npos.get_filename() == "test.css");
                    CATCH_REQUIRE(npos.get_page() == 1);
                    CATCH_REQUIRE(npos.get_line() == 1);
                    CATCH_REQUIRE(npos.get_total_line() == 1);
                }
                CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
            }
        }
    }

    // invalid escape character
    {
        std::stringstream ss;
        ss << "1.25e\\\n";
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // dimension
        {
            // a dimension is an integer or a decimal number
            // with a string expressing the dimension
            csspp::node::pointer_t dimension(l.next_token());
            CATCH_REQUIRE(dimension->is(csspp::node_type_t::DECIMAL_NUMBER));
            CATCH_REQUIRE(fabs(dimension->get_decimal_number() - 1.25) < 0.00001);
            CATCH_REQUIRE(dimension->get_string() == "e");
            csspp::position const & npos(dimension->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        VERIFY_ERRORS("test.css(1): error: spurious newline character after a \\ character outside of a string.\n");
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Percent", "[lexer] [number] [percent]")
{
    // percent with integers, converts to decimal number anyway
    {
        for(int i(-10000); i <= 10000; ++i)
        {
            std::stringstream ss;
            ss << i << "%"
               << "," << i << "\\%"
               << "," << i << "\\25"
               << "," << i << " " << "%"; // and when spaced, it becomes MODULO
            csspp::position pos("test.css");
            csspp::lexer l(ss, pos);

            // sign
            //if(i < 0)
            //{
            //    csspp::node::pointer_t subtract(l.next_token());
            //    CATCH_REQUIRE(subtract->is(csspp::node_type_t::SUBTRACT));
            //    csspp::position const & npos(subtract->get_position());
            //    CATCH_REQUIRE(npos.get_filename() == "test.css");
            //    CATCH_REQUIRE(npos.get_page() == 1);
            //    CATCH_REQUIRE(npos.get_line() == 1);
            //    CATCH_REQUIRE(npos.get_total_line() == 1);
            //}

            // percent
            {
                csspp::node::pointer_t percent(l.next_token());
                CATCH_REQUIRE(percent->is(csspp::node_type_t::PERCENT));
                CATCH_REQUIRE(SNAP_CATCH2_NAMESPACE::nearly_equal(percent->get_decimal_number(), static_cast<csspp::decimal_number_t>(i) / 100.0, 0.0));
                csspp::position const & npos(percent->get_position());
                CATCH_REQUIRE(npos.get_filename() == "test.css");
                CATCH_REQUIRE(npos.get_page() == 1);
                CATCH_REQUIRE(npos.get_line() == 1);
                CATCH_REQUIRE(npos.get_total_line() == 1);
            }

            // comma
            {
                csspp::node::pointer_t comma(l.next_token());
                CATCH_REQUIRE(comma->is(csspp::node_type_t::COMMA));
                csspp::position const & npos(comma->get_position());
                CATCH_REQUIRE(npos.get_filename() == "test.css");
                CATCH_REQUIRE(npos.get_page() == 1);
                CATCH_REQUIRE(npos.get_line() == 1);
                CATCH_REQUIRE(npos.get_total_line() == 1);
            }

            // sign
            //if(i < 0)
            //{
            //    csspp::node::pointer_t subtract(l.next_token());
            //    CATCH_REQUIRE(subtract->is(csspp::node_type_t::SUBTRACT));
            //    csspp::position const & npos(subtract->get_position());
            //    CATCH_REQUIRE(npos.get_filename() == "test.css");
            //    CATCH_REQUIRE(npos.get_page() == 1);
            //    CATCH_REQUIRE(npos.get_line() == 1);
            //    CATCH_REQUIRE(npos.get_total_line() == 1);
            //}

            // dimension (because '%' written '\%' is not a PERCENT...)
            {
                csspp::node::pointer_t integer(l.next_token());
                CATCH_REQUIRE(integer->is(csspp::node_type_t::INTEGER));
                CATCH_REQUIRE(integer->get_integer() == i);
                CATCH_REQUIRE(integer->get_string() == "%");
                csspp::position const & npos(integer->get_position());
                CATCH_REQUIRE(npos.get_filename() == "test.css");
                CATCH_REQUIRE(npos.get_page() == 1);
                CATCH_REQUIRE(npos.get_line() == 1);
                CATCH_REQUIRE(npos.get_total_line() == 1);
            }

            // comma
            {
                csspp::node::pointer_t comma(l.next_token());
                CATCH_REQUIRE(comma->is(csspp::node_type_t::COMMA));
                csspp::position const & npos(comma->get_position());
                CATCH_REQUIRE(npos.get_filename() == "test.css");
                CATCH_REQUIRE(npos.get_page() == 1);
                CATCH_REQUIRE(npos.get_line() == 1);
                CATCH_REQUIRE(npos.get_total_line() == 1);
            }

            // sign
            //if(i < 0)
            //{
            //    csspp::node::pointer_t subtract(l.next_token());
            //    CATCH_REQUIRE(subtract->is(csspp::node_type_t::SUBTRACT));
            //    csspp::position const & npos(subtract->get_position());
            //    CATCH_REQUIRE(npos.get_filename() == "test.css");
            //    CATCH_REQUIRE(npos.get_page() == 1);
            //    CATCH_REQUIRE(npos.get_line() == 1);
            //    CATCH_REQUIRE(npos.get_total_line() == 1);
            //}

            // dimension (again \25 is not a PERCENT)
            {
                csspp::node::pointer_t dimension(l.next_token());
                CATCH_REQUIRE(dimension->is(csspp::node_type_t::INTEGER));
                CATCH_REQUIRE(dimension->get_integer() == i);
                CATCH_REQUIRE(dimension->get_string() == "%");
                csspp::position const & npos(dimension->get_position());
                CATCH_REQUIRE(npos.get_filename() == "test.css");
                CATCH_REQUIRE(npos.get_page() == 1);
                CATCH_REQUIRE(npos.get_line() == 1);
                CATCH_REQUIRE(npos.get_total_line() == 1);
            }

            // comma
            {
                csspp::node::pointer_t comma(l.next_token());
                CATCH_REQUIRE(comma->is(csspp::node_type_t::COMMA));
                csspp::position const & npos(comma->get_position());
                CATCH_REQUIRE(npos.get_filename() == "test.css");
                CATCH_REQUIRE(npos.get_page() == 1);
                CATCH_REQUIRE(npos.get_line() == 1);
                CATCH_REQUIRE(npos.get_total_line() == 1);
            }

            // sign
            //if(i < 0)
            //{
            //    csspp::node::pointer_t subtract(l.next_token());
            //    CATCH_REQUIRE(subtract->is(csspp::node_type_t::SUBTRACT));
            //    csspp::position const & npos(subtract->get_position());
            //    CATCH_REQUIRE(npos.get_filename() == "test.css");
            //    CATCH_REQUIRE(npos.get_page() == 1);
            //    CATCH_REQUIRE(npos.get_line() == 1);
            //    CATCH_REQUIRE(npos.get_total_line() == 1);
            //}

            // integer (separated!)
            {
                csspp::node::pointer_t integer(l.next_token());
                CATCH_REQUIRE(integer->is(csspp::node_type_t::INTEGER));
                CATCH_REQUIRE(integer->get_integer() == i);
                CATCH_REQUIRE(integer->get_string() == "");
                CATCH_REQUIRE(integer->get_string() == "");
                csspp::position const & npos(integer->get_position());
                CATCH_REQUIRE(npos.get_filename() == "test.css");
                CATCH_REQUIRE(npos.get_page() == 1);
                CATCH_REQUIRE(npos.get_line() == 1);
                CATCH_REQUIRE(npos.get_total_line() == 1);
            }

            // whitespace
            {
                csspp::node::pointer_t whitespace(l.next_token());
                CATCH_REQUIRE(whitespace->is(csspp::node_type_t::WHITESPACE));
                csspp::position const & npos(whitespace->get_position());
                CATCH_REQUIRE(npos.get_filename() == "test.css");
                CATCH_REQUIRE(npos.get_page() == 1);
                CATCH_REQUIRE(npos.get_line() == 1);
                CATCH_REQUIRE(npos.get_total_line() == 1);
            }

            // "percent" by itself is MODULO
            {
                CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::MODULO));
                CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

                VERIFY_ERRORS("");
            }
        }
    }

    // percent directly with decimal numbers
    {
        for(int i(-1000); i <= 1000; ++i)
        {
            std::stringstream ss;
            ss << (i < 0 ? "-" : "") << abs(i) / 100 << "." << std::setw(2) << std::setfill('0') << abs(i % 100) << "%"
               << "," << (i < 0 ? "-" : "") << abs(i) / 100 << "." << std::setw(2) << abs(i % 100) << " " << "%"; // prove it does not work when we have a space
            csspp::position pos("test.css");
            csspp::lexer l(ss, pos);

            // sign
            //if(i < 0)
            //{
            //    csspp::node::pointer_t subtract(l.next_token());
            //    CATCH_REQUIRE(subtract->is(csspp::node_type_t::SUBTRACT));
            //    csspp::position const & npos(subtract->get_position());
            //    CATCH_REQUIRE(npos.get_filename() == "test.css");
            //    CATCH_REQUIRE(npos.get_page() == 1);
            //    CATCH_REQUIRE(npos.get_line() == 1);
            //    CATCH_REQUIRE(npos.get_total_line() == 1);
            //}

            // percent
            {
                csspp::node::pointer_t percent(l.next_token());
                CATCH_REQUIRE(percent->is(csspp::node_type_t::PERCENT));
                CATCH_REQUIRE(fabs(percent->get_decimal_number() - i / 10000.0) < 0.00001);
                csspp::position const & npos(percent->get_position());
                CATCH_REQUIRE(npos.get_filename() == "test.css");
                CATCH_REQUIRE(npos.get_page() == 1);
                CATCH_REQUIRE(npos.get_line() == 1);
                CATCH_REQUIRE(npos.get_total_line() == 1);
            }

            // comma
            {
                csspp::node::pointer_t comma(l.next_token());
                CATCH_REQUIRE(comma->is(csspp::node_type_t::COMMA));
                csspp::position const & npos(comma->get_position());
                CATCH_REQUIRE(npos.get_filename() == "test.css");
                CATCH_REQUIRE(npos.get_page() == 1);
                CATCH_REQUIRE(npos.get_line() == 1);
                CATCH_REQUIRE(npos.get_total_line() == 1);
            }

            // sign
            //if(i < 0)
            //{
            //    csspp::node::pointer_t subtract(l.next_token());
            //    CATCH_REQUIRE(subtract->is(csspp::node_type_t::SUBTRACT));
            //    csspp::position const & npos(subtract->get_position());
            //    CATCH_REQUIRE(npos.get_filename() == "test.css");
            //    CATCH_REQUIRE(npos.get_page() == 1);
            //    CATCH_REQUIRE(npos.get_line() == 1);
            //    CATCH_REQUIRE(npos.get_total_line() == 1);
            //}

            // decimal number (separated!)
            {
                csspp::node::pointer_t decimal_number(l.next_token());
                CATCH_REQUIRE(decimal_number->is(csspp::node_type_t::DECIMAL_NUMBER));
                CATCH_REQUIRE(fabs(decimal_number->get_decimal_number() - i / 100.0) < 0.00001);
                CATCH_REQUIRE(decimal_number->get_string() == "");
                csspp::position const & npos(decimal_number->get_position());
                CATCH_REQUIRE(npos.get_filename() == "test.css");
                CATCH_REQUIRE(npos.get_page() == 1);
                CATCH_REQUIRE(npos.get_line() == 1);
                CATCH_REQUIRE(npos.get_total_line() == 1);
            }

            // whitespace
            {
                csspp::node::pointer_t whitespace(l.next_token());
                CATCH_REQUIRE(whitespace->is(csspp::node_type_t::WHITESPACE));
                csspp::position const & npos(whitespace->get_position());
                CATCH_REQUIRE(npos.get_filename() == "test.css");
                CATCH_REQUIRE(npos.get_page() == 1);
                CATCH_REQUIRE(npos.get_line() == 1);
                CATCH_REQUIRE(npos.get_total_line() == 1);
            }

            // "percent" by itself is MODULO
            {
                CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::MODULO));
                CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

                VERIFY_ERRORS("");
            }
        }
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Unicode range", "[lexer] [unicode]")
{
    // a small test to make sure we get U or u as identifiers when
    // the + is not followed by the right character
    {
        std::stringstream ss;
        ss << "U+U or u+u";
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        // identifier
        {
            csspp::node::pointer_t identifier(l.next_token());
            CATCH_REQUIRE(identifier->is(csspp::node_type_t::IDENTIFIER));
            CATCH_REQUIRE(identifier->get_string() == "U");
            csspp::position const & npos(identifier->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // add
        {
            csspp::node::pointer_t add(l.next_token());
            CATCH_REQUIRE(add->is(csspp::node_type_t::ADD));
            csspp::position const & npos(add->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // identifier
        {
            csspp::node::pointer_t identifier(l.next_token());
            CATCH_REQUIRE(identifier->is(csspp::node_type_t::IDENTIFIER));
            CATCH_REQUIRE(identifier->get_string() == "U");
            csspp::position const & npos(identifier->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // whitespace
        {
            csspp::node::pointer_t whitespace(l.next_token());
            CATCH_REQUIRE(whitespace->is(csspp::node_type_t::WHITESPACE));
            csspp::position const & npos(whitespace->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // identifier
        {
            csspp::node::pointer_t identifier(l.next_token());
            CATCH_REQUIRE(identifier->is(csspp::node_type_t::IDENTIFIER));
            CATCH_REQUIRE(identifier->get_string() == "or");
            csspp::position const & npos(identifier->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // whitespace
        {
            csspp::node::pointer_t whitespace(l.next_token());
            CATCH_REQUIRE(whitespace->is(csspp::node_type_t::WHITESPACE));
            csspp::position const & npos(whitespace->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // identifier
        {
            csspp::node::pointer_t identifier(l.next_token());
            CATCH_REQUIRE(identifier->is(csspp::node_type_t::IDENTIFIER));
            CATCH_REQUIRE(identifier->get_string() == "u");
            csspp::position const & npos(identifier->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // add
        {
            csspp::node::pointer_t add(l.next_token());
            CATCH_REQUIRE(add->is(csspp::node_type_t::ADD));
            csspp::position const & npos(add->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // identifier
        {
            csspp::node::pointer_t identifier(l.next_token());
            CATCH_REQUIRE(identifier->is(csspp::node_type_t::IDENTIFIER));
            CATCH_REQUIRE(identifier->get_string() == "u");
            csspp::position const & npos(identifier->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
    }

    // one value (U+<value>)
    // two values with the same number (U+<value>-<value>)
    // two values with the same number, but force 6 digits each (U+<value>-<value>)
    // check the first 64Kb (plan 0) and then randomize
    {
        for(csspp::wide_char_t unicode(0); unicode < 65536; ++unicode)
        {
            std::stringstream ss;
            ss << (rand() & 1 ? "U" : "u") << "+" << std::hex << unicode
               << "," << (rand() & 1 ? "U" : "u") << "+" << std::hex << unicode << "-" << unicode
               << "," << (rand() & 1 ? "U" : "u") << "+" << std::hex << std::setw(6) << std::setfill('0') << unicode << "-" << std::setw(6) << unicode << std::setfill('\0');
            csspp::position pos("test.css");
            csspp::lexer l(ss, pos);

            csspp_test::our_unicode_range_t range(unicode, unicode);

            // unicode range
            {
                csspp::node::pointer_t unicode_range(l.next_token());
                CATCH_REQUIRE(unicode_range->is(csspp::node_type_t::UNICODE_RANGE));
                CATCH_REQUIRE(unicode_range->get_integer() == static_cast<csspp::integer_t>(range.get_range()));
                csspp::position const & npos(unicode_range->get_position());
                CATCH_REQUIRE(npos.get_filename() == "test.css");
                CATCH_REQUIRE(npos.get_page() == 1);
                CATCH_REQUIRE(npos.get_line() == 1);
                CATCH_REQUIRE(npos.get_total_line() == 1);
            }

            // comma
            {
                csspp::node::pointer_t comma(l.next_token());
                CATCH_REQUIRE(comma->is(csspp::node_type_t::COMMA));
                csspp::position const & npos(comma->get_position());
                CATCH_REQUIRE(npos.get_filename() == "test.css");
                CATCH_REQUIRE(npos.get_page() == 1);
                CATCH_REQUIRE(npos.get_line() == 1);
                CATCH_REQUIRE(npos.get_total_line() == 1);
            }

            // unicode range
            {
                csspp::node::pointer_t unicode_range(l.next_token());
                CATCH_REQUIRE(unicode_range->is(csspp::node_type_t::UNICODE_RANGE));
                CATCH_REQUIRE(unicode_range->get_integer() == static_cast<csspp::integer_t>(range.get_range()));
                csspp::position const & npos(unicode_range->get_position());
                CATCH_REQUIRE(npos.get_filename() == "test.css");
                CATCH_REQUIRE(npos.get_page() == 1);
                CATCH_REQUIRE(npos.get_line() == 1);
                CATCH_REQUIRE(npos.get_total_line() == 1);
            }

            // comma
            {
                csspp::node::pointer_t comma(l.next_token());
                CATCH_REQUIRE(comma->is(csspp::node_type_t::COMMA));
                csspp::position const & npos(comma->get_position());
                CATCH_REQUIRE(npos.get_filename() == "test.css");
                CATCH_REQUIRE(npos.get_page() == 1);
                CATCH_REQUIRE(npos.get_line() == 1);
                CATCH_REQUIRE(npos.get_total_line() == 1);
            }

            // unicode range
            {
                csspp::node::pointer_t unicode_range(l.next_token());
                CATCH_REQUIRE(unicode_range->is(csspp::node_type_t::UNICODE_RANGE));
                CATCH_REQUIRE(unicode_range->get_integer() == static_cast<csspp::integer_t>(range.get_range()));
                csspp::position const & npos(unicode_range->get_position());
                CATCH_REQUIRE(npos.get_filename() == "test.css");
                CATCH_REQUIRE(npos.get_page() == 1);
                CATCH_REQUIRE(npos.get_line() == 1);
                CATCH_REQUIRE(npos.get_total_line() == 1);
            }

            CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
        }

        for(int i(0); i < 1000; ++i)
        {
            csspp::wide_char_t unicode(rand() % 0x110000);
            std::stringstream ss;
            ss << (rand() & 1 ? "U" : "u") << "+" << std::hex << unicode
               << "," << (rand() & 1 ? "U" : "u") << "+" << std::hex << unicode << "-" << unicode
               << "," << (rand() & 1 ? "U" : "u") << "+" << std::hex << std::setw(6) << std::setfill('0') << unicode << "-" << std::setw(6) << unicode << std::setfill('\0');
            csspp::position pos("test.css");
            csspp::lexer l(ss, pos);

            csspp_test::our_unicode_range_t range(unicode, unicode);

            // unicode range
            {
                csspp::node::pointer_t unicode_range(l.next_token());
                CATCH_REQUIRE(unicode_range->is(csspp::node_type_t::UNICODE_RANGE));
                CATCH_REQUIRE(unicode_range->get_integer() == static_cast<csspp::integer_t>(range.get_range()));
                csspp::position const & npos(unicode_range->get_position());
                CATCH_REQUIRE(npos.get_filename() == "test.css");
                CATCH_REQUIRE(npos.get_page() == 1);
                CATCH_REQUIRE(npos.get_line() == 1);
                CATCH_REQUIRE(npos.get_total_line() == 1);
            }

            // comma
            {
                csspp::node::pointer_t comma(l.next_token());
                CATCH_REQUIRE(comma->is(csspp::node_type_t::COMMA));
                csspp::position const & npos(comma->get_position());
                CATCH_REQUIRE(npos.get_filename() == "test.css");
                CATCH_REQUIRE(npos.get_page() == 1);
                CATCH_REQUIRE(npos.get_line() == 1);
                CATCH_REQUIRE(npos.get_total_line() == 1);
            }

            // unicode range
            {
                csspp::node::pointer_t unicode_range(l.next_token());
                CATCH_REQUIRE(unicode_range->is(csspp::node_type_t::UNICODE_RANGE));
                CATCH_REQUIRE(unicode_range->get_integer() == static_cast<csspp::integer_t>(range.get_range()));
                csspp::position const & npos(unicode_range->get_position());
                CATCH_REQUIRE(npos.get_filename() == "test.css");
                CATCH_REQUIRE(npos.get_page() == 1);
                CATCH_REQUIRE(npos.get_line() == 1);
                CATCH_REQUIRE(npos.get_total_line() == 1);
            }

            // comma
            {
                csspp::node::pointer_t comma(l.next_token());
                CATCH_REQUIRE(comma->is(csspp::node_type_t::COMMA));
                csspp::position const & npos(comma->get_position());
                CATCH_REQUIRE(npos.get_filename() == "test.css");
                CATCH_REQUIRE(npos.get_page() == 1);
                CATCH_REQUIRE(npos.get_line() == 1);
                CATCH_REQUIRE(npos.get_total_line() == 1);
            }

            // unicode range
            {
                csspp::node::pointer_t unicode_range(l.next_token());
                CATCH_REQUIRE(unicode_range->is(csspp::node_type_t::UNICODE_RANGE));
                CATCH_REQUIRE(unicode_range->get_integer() == static_cast<csspp::integer_t>(range.get_range()));
                csspp::position const & npos(unicode_range->get_position());
                CATCH_REQUIRE(npos.get_filename() == "test.css");
                CATCH_REQUIRE(npos.get_page() == 1);
                CATCH_REQUIRE(npos.get_line() == 1);
                CATCH_REQUIRE(npos.get_total_line() == 1);
            }

            CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
        }
    }

    // test that we recover the identifier right after a Unicode Range
    {
        std::stringstream ss;
        csspp::wide_char_t unicode(rand() % 0x110000);
        ss << (rand() & 1 ? "U" : "u") << "+" << std::hex << std::setw(6) << std::setfill('0') << unicode << "Alexis";
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        csspp_test::our_unicode_range_t range(unicode, unicode);

        // unicode range
        {
            csspp::node::pointer_t unicode_range(l.next_token());
            CATCH_REQUIRE(unicode_range->is(csspp::node_type_t::UNICODE_RANGE));
            CATCH_REQUIRE(unicode_range->get_integer() == static_cast<csspp::integer_t>(range.get_range()));
            csspp::position const & npos(unicode_range->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // identifier
        {
            csspp::node::pointer_t identifier(l.next_token());
            CATCH_REQUIRE(identifier->is(csspp::node_type_t::IDENTIFIER));
            CATCH_REQUIRE(identifier->get_string() == "Alexis");
            csspp::position const & npos(identifier->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
    }

    // test that we recover the number right after a Unicode Range
    {
        std::stringstream ss;
        csspp::wide_char_t unicode(rand() % 0x110000);
        ss << (rand() & 1 ? "U" : "u") << "+" << std::hex << std::setw(6) << std::setfill('0') << unicode << "123";
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        csspp_test::our_unicode_range_t range(unicode, unicode);

        // unicode range
        {
            csspp::node::pointer_t unicode_range(l.next_token());
            CATCH_REQUIRE(unicode_range->is(csspp::node_type_t::UNICODE_RANGE));
            CATCH_REQUIRE(unicode_range->get_integer() == static_cast<csspp::integer_t>(range.get_range()));
            csspp::position const & npos(unicode_range->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // integer
        {
            csspp::node::pointer_t integer(l.next_token());
            CATCH_REQUIRE(integer->is(csspp::node_type_t::INTEGER));
            CATCH_REQUIRE(integer->get_integer() == 123);
            csspp::position const & npos(integer->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
    }

    // try various masks
    {
        // mask of 6 needs to be tested once
        std::stringstream ss;
        ss << (rand() & 1 ? "U" : "u") << "+??????";
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        csspp_test::our_unicode_range_t range(0, 0x1FFFFF);

        // unicode range
        {
            csspp::node::pointer_t unicode_range(l.next_token());
            CATCH_REQUIRE(unicode_range->is(csspp::node_type_t::UNICODE_RANGE));
            CATCH_REQUIRE(unicode_range->get_integer() == static_cast<csspp::integer_t>(range.get_range()));
            csspp::position const & npos(unicode_range->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
    }
    for(int i(0); i < 3; i++)
    {
        // mask of 5 can be checked three times: ?????, 0?????, 1?????
        int const mask_count(5);
        int const mask((1 << (mask_count * 4)) - 1);
        std::stringstream ss;
        csspp::wide_char_t unicode(i == 2 ? 0x100000 : 0);
        ss << std::hex << std::setw(6) << std::setfill('0') << unicode;
        std::string unicode_str(ss.str());
        ss.str("");
        for(size_t p(unicode_str.length() - mask_count); p < unicode_str.length(); ++p)
        {
            // replace by the mask (i.e. '?')
            unicode_str[p] = '?';
        }
        if(i == 0)
        {
            // remove leading zeroes
            while(unicode_str.front() == '0')
            {
                unicode_str.erase(unicode_str.begin());
            }
        }
        ss << (rand() & 1 ? "U" : "u") << "+" << unicode_str;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        csspp_test::our_unicode_range_t range(unicode, unicode | mask);

        // unicode range
        {
            csspp::node::pointer_t unicode_range(l.next_token());
            CATCH_REQUIRE(unicode_range->is(csspp::node_type_t::UNICODE_RANGE));
            CATCH_REQUIRE(unicode_range->get_integer() == static_cast<csspp::integer_t>(range.get_range()));
            csspp::position const & npos(unicode_range->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
    }
    for(int i(0); i < 0x11; ++i)
    {
        // mask of 4 needs to be tested from 0x00 to 0x10
        // (we could also check with the leading zero and without
        // for each value, but that's not too important)
        int const mask_count(4);
        int const mask((1 << (mask_count * 4)) - 1);
        std::stringstream ss;
        csspp::wide_char_t unicode(i << 16); //(rand() % 0x110000) & ~mask);
        ss << std::hex << std::setw(6) << std::setfill('0') << unicode;
        std::string unicode_str(ss.str());
        ss.str("");
        for(size_t p(unicode_str.length() - mask_count); p < unicode_str.length(); ++p)
        {
            // replace by the mask (i.e. '?')
            unicode_str[p] = '?';
        }
        if(rand() % 3 != 0)
        {
            // remove leading zeroes
            while(unicode_str.front() == '0')
            {
                unicode_str.erase(unicode_str.begin());
            }
        }
        ss << (rand() & 1 ? "U" : "u") << "+" << unicode_str;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        csspp_test::our_unicode_range_t range(unicode, unicode | mask);

        // unicode range
        {
            csspp::node::pointer_t unicode_range(l.next_token());
            CATCH_REQUIRE(unicode_range->is(csspp::node_type_t::UNICODE_RANGE));
            CATCH_REQUIRE(unicode_range->get_integer() == static_cast<csspp::integer_t>(range.get_range()));
            csspp::position const & npos(unicode_range->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
    }
    for(int i(0); i < 1000; ++i) //1433656549
    {
        // a few random mask of 1 to 3 '?'
        int const mask_count(rand() % 3 + 1);
        int const mask((1 << (mask_count * 4)) - 1);
        std::stringstream ss;
        csspp::wide_char_t unicode((rand() % 0x110000) & ~mask);
        ss << std::hex << std::setw(6) << std::setfill('0') << unicode;
        std::string unicode_str(ss.str());
        ss.str("");
        for(size_t p(unicode_str.length() - mask_count); p < unicode_str.length(); ++p)
        {
            // replace by the mask (i.e. '?')
            unicode_str[p] = '?';
        }
        if(rand() % 3 != 0)
        {
            // remove leading zeroes
            while(unicode_str.front() == '0')
            {
                unicode_str.erase(unicode_str.begin());
            }
        }
        ss << (rand() & 1 ? "U" : "u") << "+" << unicode_str;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        csspp_test::our_unicode_range_t range(unicode, unicode | mask);

        // unicode range
        {
            csspp::node::pointer_t unicode_range(l.next_token());
            CATCH_REQUIRE(unicode_range->is(csspp::node_type_t::UNICODE_RANGE));
            CATCH_REQUIRE(unicode_range->get_integer() == static_cast<csspp::integer_t>(range.get_range()));
            csspp::position const & npos(unicode_range->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
    }

    // test simple range (start only) with values that are too large
    for(int i(0); i < 1000; i++)
    {
        // check a start unicode value too large
        {
            std::stringstream ss;
            // an invalid value which is exactly 6 hexadecimal digits
            csspp::wide_char_t unicode(rand() % (0x1000000 - 0x110000) + 0x110000);
            ss << (rand() & 1 ? "U" : "u") << "+" << std::hex << unicode;
            csspp::position pos("test.css");
            csspp::lexer l(ss, pos);

            // unicode range
            {
                csspp::node::pointer_t unicode_range(l.next_token());
                CATCH_REQUIRE(unicode_range->is(csspp::node_type_t::UNICODE_RANGE));
                //CATCH_REQUIRE(unicode_range->get_integer() == range.f_range); -- there was an overflow
                csspp::position const & npos(unicode_range->get_position());
                CATCH_REQUIRE(npos.get_filename() == "test.css");
                CATCH_REQUIRE(npos.get_page() == 1);
                CATCH_REQUIRE(npos.get_line() == 1);
                CATCH_REQUIRE(npos.get_total_line() == 1);

                VERIFY_ERRORS("test.css(1): error: unicode character too large, range is U+000000 to U+10FFFF.\n");
            }

            CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
        }

        // check with the second unicode too large
        {
            std::stringstream ss;
            // an invalid value which is exactly 6 hexadecimal digits
            csspp::wide_char_t unicode_start(rand() % 0x110000);
            csspp::wide_char_t unicode_end(rand() % (0x1000000 - 0x110000) + 0x110000);
            ss << (rand() & 1 ? "U" : "u") << "+" << std::hex << unicode_start << "-" << unicode_end;
            csspp::position pos("test.css");
            csspp::lexer l(ss, pos);

            // unicode range
            {
                csspp::node::pointer_t unicode_range(l.next_token());
                CATCH_REQUIRE(unicode_range->is(csspp::node_type_t::UNICODE_RANGE));
                //CATCH_REQUIRE(unicode_range->get_integer() == range.f_range); -- there was an overflow
                csspp::position const & npos(unicode_range->get_position());
                CATCH_REQUIRE(npos.get_filename() == "test.css");
                CATCH_REQUIRE(npos.get_page() == 1);
                CATCH_REQUIRE(npos.get_line() == 1);
                CATCH_REQUIRE(npos.get_total_line() == 1);

                VERIFY_ERRORS("test.css(1): error: unicode character too large, range is U+000000 to U+10FFFF.\n");
            }

            CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
        }

        // check with a mask which is too large
        {
            // WARNING: this test works with a mask of 1 to 4 x '?'
            //          with 5, you would have to make sure that the first
            //          Unicode digit it 2 or more
            //          with 6, it never fails (and it is tested earlier)
            //
            int const mask_count(rand() % 3 + 1);
            int const mask((1 << (mask_count * 4)) - 1);
            std::stringstream ss;
            // an invalid value which is exactly 6 hexadecimal digits
            csspp::wide_char_t const unicode((rand() % (0x1000000 - 0x110000) + 0x110000) & ~mask);
            ss << std::hex << unicode;
            std::string unicode_str(ss.str());
            ss.str("");
            for(size_t p(unicode_str.length() - mask_count); p < unicode_str.length(); ++p)
            {
                // replace by the mask (i.e. '?')
                unicode_str[p] = '?';
            }
            ss << (rand() & 1 ? "U" : "u") << "+" << unicode_str;
            csspp::position pos("test.css");
            csspp::lexer l(ss, pos);

            // unicode range
            {
                csspp::node::pointer_t unicode_range(l.next_token());
                CATCH_REQUIRE(unicode_range->is(csspp::node_type_t::UNICODE_RANGE));
                //CATCH_REQUIRE(unicode_range->get_integer() == range.f_range); -- there was an overflow, what could we check?
                csspp::position const & npos(unicode_range->get_position());
                CATCH_REQUIRE(npos.get_filename() == "test.css");
                CATCH_REQUIRE(npos.get_page() == 1);
                CATCH_REQUIRE(npos.get_line() == 1);
                CATCH_REQUIRE(npos.get_total_line() == 1);

                VERIFY_ERRORS("test.css(1): error: unicode character too large, range is U+000000 to U+10FFFF.\n");
            }

            CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
        }
    }

    // actual range: in order and not in order
    for(int i(0); i < 1000; i++)
    {
        std::stringstream ss;
        // an invalid value which is exactly 6 hexadecimal digits
        csspp::wide_char_t unicode_start(rand() % 0x110000);
        csspp::wide_char_t unicode_end(rand() % 0x110000);
        // avoid equality (already tested!)
        while(unicode_end == unicode_start)
        {
            unicode_end = rand() % 0x110000;
        }
        // make sure start is smaller
        if(unicode_start > unicode_end)
        {
            std::swap(unicode_start, unicode_end);
        }
        // test both: valid and invalid ranges
        ss << (rand() & 1 ? "U" : "u") << "+" << std::hex << unicode_start << "-" << unicode_end
           << "," << (rand() & 1 ? "U" : "u") << "+" << unicode_end << "-" << unicode_start;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);

        csspp_test::our_unicode_range_t range(unicode_start, unicode_end);

        // unicode range
        {
            csspp::node::pointer_t unicode_range(l.next_token());
            CATCH_REQUIRE(unicode_range->is(csspp::node_type_t::UNICODE_RANGE));
            CATCH_REQUIRE(unicode_range->get_integer() == static_cast<csspp::integer_t>(range.get_range()));
            csspp::position const & npos(unicode_range->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // comma
        {
            csspp::node::pointer_t whitespace(l.next_token());
            CATCH_REQUIRE(whitespace->is(csspp::node_type_t::COMMA));
            csspp::position const & npos(whitespace->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // unicode range
        {
            csspp::node::pointer_t unicode_range(l.next_token());
            CATCH_REQUIRE(unicode_range->is(csspp::node_type_t::UNICODE_RANGE));
            //CATCH_REQUIRE(unicode_range->get_integer() == range.f_range); -- we get an error, we know what the range is, but we do not want to assume so in the test
            csspp::position const & npos(unicode_range->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);

            VERIFY_ERRORS("test.css(1): error: unicode range cannot have a start character larger than the end character.\n");
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Hash", "[lexer] [hash]")
{
    // test a standard hash
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << "#-escape\\=33-";

        // hash
        {
            csspp::node::pointer_t identifier(l.next_token());
            CATCH_REQUIRE(identifier->is(csspp::node_type_t::HASH));
            CATCH_REQUIRE(identifier->get_string() == "-escape=33-");
            csspp::position const & npos(identifier->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
    }

    // generate a set of simple and valid hashes
    for(int i(0); i < 1000; ++i)
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << '#';
        int count(rand() % 30 + 1);
        std::string word;
        for(int j(0); j < count; ++j)
        {
            csspp::wide_char_t c(0);
            for(;;)
            {
                c = rand() % 0x110000;
                if((c >= 'A' && c <= 'Z')
                || (c >= 'a' && c <= 'z')
                || (c >= '0' && c <= '9')
                || c == '_'
                || c == '-'
                ||   (c > 0x80
                   && c != 0xFFFD
                   && (c < 0xD800 || c > 0xDFFF)
                   && ((c & 0xFFFF) != 0xFFFE)
                   && ((c & 0xFFFF) != 0xFFFF))
                )
                {
                    break;
                }
            }
            word += l.wctomb(c);
        }
        ss << word;

        // hash
        {
            csspp::node::pointer_t identifier(l.next_token());
            CATCH_REQUIRE(identifier->is(csspp::node_type_t::HASH));
            CATCH_REQUIRE(identifier->get_string() == word);
            csspp::position const & npos(identifier->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
    }

    // test a standard hash
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << "#-escape\\0 33-";

        // hash
        {
            csspp::node::pointer_t hash(l.next_token());
            CATCH_REQUIRE(hash->is(csspp::node_type_t::HASH));
            CATCH_REQUIRE(hash->get_string() == "-escape");
            csspp::position const & npos(hash->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);

            VERIFY_ERRORS("test.css(1): error: escape character '\\0' is not acceptable in CSS.\n");
        }

        // integer
        {
            csspp::node::pointer_t identifier(l.next_token());
            CATCH_REQUIRE(identifier->is(csspp::node_type_t::INTEGER));
            CATCH_REQUIRE(identifier->get_integer() == 33);
            csspp::position const & npos(identifier->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // subtract
        {
            csspp::node::pointer_t subtract(l.next_token());
            CATCH_REQUIRE(subtract->is(csspp::node_type_t::SUBTRACT));
            csspp::position const & npos(subtract->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
    }
}

CATCH_TEST_CASE("Invalid hash", "[lexer] [hash]")
{
    // test an empty hash
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << "empty # here";

        // identifier (empty)
        {
            csspp::node::pointer_t hash(l.next_token());
            CATCH_REQUIRE(hash->is(csspp::node_type_t::IDENTIFIER));
            CATCH_REQUIRE(hash->get_string() == "empty");
            csspp::position const & npos(hash->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);

            VERIFY_ERRORS("");
        }

        // whitespace
        {
            csspp::node::pointer_t hash(l.next_token());
            CATCH_REQUIRE(hash->is(csspp::node_type_t::WHITESPACE));
            csspp::position const & npos(hash->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);

            VERIFY_ERRORS("");
        }

        // hash
        // '#" by itself generates an error and nothing is returned

        // whitespace
        {
            csspp::node::pointer_t hash(l.next_token());
            CATCH_REQUIRE(hash->is(csspp::node_type_t::WHITESPACE));
            csspp::position const & npos(hash->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);

            VERIFY_ERRORS("test.css(1): error: '#' by itself is not valid.\n");
        }

        // identifier (here)
        {
            csspp::node::pointer_t identifier(l.next_token());
            CATCH_REQUIRE(identifier->is(csspp::node_type_t::IDENTIFIER));
            CATCH_REQUIRE(identifier->get_string() == "here");
            csspp::position const & npos(identifier->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Placeholders", "[lexer] [hash]")
{
    // test a standard placeholder
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << "%es-cape\\=33-";

        // hash
        {
            csspp::node::pointer_t identifier(l.next_token());
            CATCH_REQUIRE(identifier->is(csspp::node_type_t::PLACEHOLDER));
            CATCH_REQUIRE(identifier->get_string() == "es-cape=33-");
            csspp::position const & npos(identifier->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
    }

    // generate a set of simple and valid placeholders
    for(int i(0); i < 1000; ++i)
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << '%';
        int count(rand() % 30 + 1);
        std::string word;
        std::string lword;
        for(int j(0); j < count; ++j)
        {
            csspp::wide_char_t c(0);
            for(;;)
            {
                c = rand() % 0x110000;
                if((c >= 'A' && c <= 'Z')
                || (c >= 'a' && c <= 'z')
                || (c >= '0' && c <= '9')
                || c == '_'
                || (c == '-' && j != 0)
                ||   (c > 0x80
                   && c != 0xFFFD
                   && (c < 0xD800 || c > 0xDFFF)
                   && ((c & 0xFFFF) != 0xFFFE)
                   && ((c & 0xFFFF) != 0xFFFF))
                )
                {
                    break;
                }
            }
            word += l.wctomb(c);
            lword += l.wctomb(std::tolower(c));
        }
        ss << word;

        // placeholder
        {
            csspp::node::pointer_t identifier(l.next_token());
            CATCH_REQUIRE(identifier->is(csspp::node_type_t::PLACEHOLDER));
            CATCH_REQUIRE(identifier->get_string() == word);
            CATCH_REQUIRE(identifier->get_lowercase_string() == lword);
            csspp::position const & npos(identifier->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
    }

    // test a standard placeholder
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        ss << "%es-cape\\0 33-";

        // placeholder
        {
            csspp::node::pointer_t hash(l.next_token());
            CATCH_REQUIRE(hash->is(csspp::node_type_t::PLACEHOLDER));
            CATCH_REQUIRE(hash->get_string() == "es-cape");
            csspp::position const & npos(hash->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);

            VERIFY_ERRORS("test.css(1): error: escape character '\\0' is not acceptable in CSS.\n");
        }

        // integer
        {
            csspp::node::pointer_t identifier(l.next_token());
            CATCH_REQUIRE(identifier->is(csspp::node_type_t::INTEGER));
            CATCH_REQUIRE(identifier->get_integer() == 33);
            csspp::position const & npos(identifier->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // subtract
        {
            csspp::node::pointer_t subtract(l.next_token());
            CATCH_REQUIRE(subtract->is(csspp::node_type_t::SUBTRACT));
            csspp::position const & npos(subtract->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Variables", "[lexer] [variable]")
{
    // test variables
    for(int i(0); i < 1000; ++i)
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        std::string word;
        std::string lword;
        int size(rand() % 20 + 1);
        for(int j(0); j < size; ++j)
        {
            // only valid characters
            char c(rand() % (10 + 26 + 26 + 2));
            if(c < 10)
            {
                c += '0';
                lword += c;
            }
            else if(c < 10 + 26)
            {
                c += 'A' - 10;
                lword += c + 0x20;
            }
            else if(c < 10 + 26 + 26)
            {
                c += 'a' - 10 - 26;
                lword += c;
            }
            else if(c < 10 + 26 + 26 + 1)
            {
                c = '-';
                lword += '_'; // '-' == '_' in variable names
            }
            else
            {
                c = '_';
                lword += '_';
            }
            word += c;
        }
        ss << "$" << word;

        // variable
        {
            csspp::node::pointer_t variable(l.next_token());
            CATCH_REQUIRE(variable->is(csspp::node_type_t::VARIABLE));
            CATCH_REQUIRE(variable->get_string() == lword);
            csspp::position const & npos(variable->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // no error left over
        VERIFY_ERRORS("");
    }

    // test variable functions
    for(int i(0); i < 1000; ++i)
    {
        std::stringstream ss;
        csspp::position pos("test.css");
        csspp::lexer l(ss, pos);
        std::string word;
        std::string lword;
        int size(rand() % 20 + 1);
        for(int j(0); j < size; ++j)
        {
            // only valid characters
            char c(rand() % (10 + 26 + 26 + 2));
            if(c < 10)
            {
                c += '0';
                lword += c;
            }
            else if(c < 10 + 26)
            {
                c += 'A' - 10;
                lword += c + 0x20;
            }
            else if(c < 10 + 26 + 26)
            {
                c += 'a' - 10 - 26;
                lword += c;
            }
            else if(c < 10 + 26 + 26 + 1)
            {
                c = '-';
                lword += '_'; // '-' == '_' in variable names
            }
            else
            {
                c = '_';
                lword += '_';
            }
            word += c;
        }
        ss << "$" << word << "(args)";

        // variable function
        {
            csspp::node::pointer_t variable(l.next_token());
            CATCH_REQUIRE(variable->is(csspp::node_type_t::VARIABLE_FUNCTION));
            CATCH_REQUIRE(variable->get_string() == lword);
            csspp::position const & npos(variable->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // args
        {
            csspp::node::pointer_t variable(l.next_token());
            CATCH_REQUIRE(variable->is(csspp::node_type_t::IDENTIFIER));
            CATCH_REQUIRE(variable->get_string() == "args");
            csspp::position const & npos(variable->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        // ')'
        {
            csspp::node::pointer_t variable(l.next_token());
            CATCH_REQUIRE(variable->is(csspp::node_type_t::CLOSE_PARENTHESIS));
            csspp::position const & npos(variable->get_position());
            CATCH_REQUIRE(npos.get_filename() == "test.css");
            CATCH_REQUIRE(npos.get_page() == 1);
            CATCH_REQUIRE(npos.get_line() == 1);
            CATCH_REQUIRE(npos.get_total_line() == 1);
        }

        CATCH_REQUIRE(l.next_token()->is(csspp::node_type_t::EOF_TOKEN));

        // no error left over
        VERIFY_ERRORS("");
    }
}

// vim: ts=4 sw=4 et
