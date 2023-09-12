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
#pragma once

/** \file
 * \brief Common header for all our catch2 tests.
 *
 * csspp comes with a unit test suite. This header defines things
 * that all the tests access, such as the snapcatch2.hpp header file.
 */

// csspp
//
#include    <csspp/unicode_range.h>


// snapcatch2
//
#include    <catch2/snapcatch2.hpp>



namespace csspp_test
{

class trace_error
{
public:
                            trace_error();

    static trace_error &    instance();

    void                    set_verbose();

    void                    expected_error(std::string const & msg, char const * filename, int line);

private:
    std::stringstream       m_error_message = std::stringstream();
    bool                    m_verbose = false;
};

#define VERIFY_ERRORS(msg)  ::csspp_test::trace_error::instance().expected_error((msg), __FILE__, __LINE__)

class our_unicode_range_t
{
public:
                            our_unicode_range_t(csspp::wide_char_t start, csspp::wide_char_t end);

    void                    set_start(csspp::wide_char_t start);
    void                    set_end(csspp::wide_char_t end);
    void                    set_range(csspp::range_value_t range);

    csspp::wide_char_t      get_start() const;
    csspp::wide_char_t      get_end() const;
    csspp::range_value_t    get_range() const;

private:
    csspp::wide_char_t      f_start = csspp::wide_char_t();
    csspp::wide_char_t      f_end = csspp::wide_char_t();
};

// this compares two resulting trees, line by line
void compare(std::string const & generated, std::string const & expected, char const * filename, int line);
#define VERIFY_TREES(a, b)  ::csspp_test::compare((a), (b), __FILE__, __LINE__)

typedef uint64_t    default_variables_flags_t;

default_variables_flags_t const         flag_no_logo_true = 0x0001;

std::string get_script_path();
std::string get_version_script_path();
std::string get_default_variables(default_variables_flags_t const flags = 0);
std::string get_close_comment(bool token = false);
time_t get_now();

} // csspp_test namespace
// vim: ts=4 sw=4 et
