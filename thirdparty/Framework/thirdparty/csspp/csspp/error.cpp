// Copyright (c) 2015-2022  Made to Order Software Corp.  All Rights Reserved
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
 * \brief Implementation of the CSS Preprocessor error handling.
 *
 * The library handles errors by printing messages to a standard output
 * stream. The functions also count the number of errors and warnings
 * that occur while parsing and compiling a source file.
 *
 * The output can be redirected to your own buffer.
 *
 * The number of errors can be \em protected by an RAII class so the
 * exact same instance of the library can be reused any number of times
 * (in case you were to create a GUI that helps debug code quickly,
 * possibly inline...)
 *
 * \code
 *      // error counts are zero
 *      csspp::error_count_t count;
 *      {
 *          csspp::safe_error_t safe_errors;
 *
 *          // super over simplified... (no proper error handling
 *          // and probably a bit wrong)
 *          csspp::position pos;
 *          csspp::lexer l(input, pos);
 *          csspp::parser p(l);
 *          csspp::node root(p.stylesheet());
 *          csspp::compiler c;
 *          c.set_root(root);
 *          c.compile()
 *
 *          // get number of errors before the '}'
 *          count = csspp::error()->get_error_count();
 *      }
 *      // error counts are still zero, they were restored by safe_error_t
 * \endcode
 *
 * \sa \ref lexer_rules
 */

#include    "csspp/error.h"

#include    <cmath>
#include    <iostream>

namespace csspp
{

namespace
{

// the error instance
error *g_error = nullptr;

} // no name namespace

error::error()
    : f_position("error.css")
{
}

error & error::instance()
{
    // first time, allocate the instance
    if(g_error == nullptr)
    {
        g_error = new error;
    }

    g_error->reset();

    return *g_error;
}

std::ostream & error::get_error_stream() const
{
    return *f_error;
}

void error::set_error_stream(std::ostream & err_stream)
{
    f_error = &err_stream;
}

void error::set_count_warnings_as_errors(bool warnings_as_errors)
{
    f_warnings_as_errors = warnings_as_errors;
}

error_count_t error::get_error_count() const
{
    return f_error_count;
}

void error::set_error_count(error_count_t count)
{
    f_error_count = count;
}

error_count_t error::get_warning_count() const
{
    return f_warning_count;
}

void error::set_warning_count(error_count_t count)
{
    f_warning_count = count;
}

void error::set_hide_all(bool hide_all)
{
    f_hide_all = hide_all;
}

void error::set_show_debug(bool show_debug)
{
    f_show_debug = show_debug;
}

void error::set_verbose(bool status)
{
    f_verbose = status;
}

error & error::operator << (position const & pos)
{
    f_position = pos;
    return *this;
}

error & error::operator << (error_mode_t mode)
{
    switch(mode)
    {
    case error_mode_t::ERROR_DEC:
        f_message << std::dec;
        break;

    case error_mode_t::ERROR_FATAL:
    case error_mode_t::ERROR_ERROR:
        ++f_error_count;
        goto print_error;

    case error_mode_t::ERROR_WARNING:
        if(f_warnings_as_errors)
        {
            // count warnings just like if they were errors
            ++f_error_count;
        }
        else
        {
            // should we count warnings even if we do not show them?
            if(f_hide_all)
            {
                break;
            }
            ++f_warning_count;
        }
        goto print_error;

    case error_mode_t::ERROR_DEBUG:
        if(!f_show_debug)
        {
            break;
        }
#if __cplusplus >= 201700
        [[fallthrough]];
#endif
    case error_mode_t::ERROR_INFO:
        if(f_hide_all)
        {
            break;
        }

print_error:
        // print the error now
        // (show the page number?)
        {
            std::ostream * out = f_error ? f_error : &std::cerr;
            *out << f_position.get_filename()
                 << "(" << f_position.get_line() << "): " << mode << ": "
                 << f_message.str()
                 << std::endl;

            // for debug purposes, otherwise errors are always hidden!
            if(f_verbose)
            {
                std::cerr << "ERROR CONSOLE OUTPUT -- " << f_position.get_filename()
                          << "(" << f_position.get_line() << "): " << mode << ": "
                          << f_message.str()
                          << std::endl;
            }
        }
        break;

    case error_mode_t::ERROR_HEX:
        f_message << std::hex;
        break;

    }

    return *this;
}

error & error::operator << (std::string const & msg)
{
    f_message << msg;
    return *this;
}

error & error::operator << (char const * msg)
{
    f_message << msg;
    return *this;
}

error & error::operator << (int32_t value)
{
    f_message << value;
    return *this;
}

error & error::operator << (int64_t value)
{
    f_message << value;
    return *this;
}

error & error::operator << (double value)
{
    f_message << value;
    return *this;
}

void error::reset()
{
    // reset the output message
    f_message.str("");
    f_message << std::dec;
}

safe_error_t::safe_error_t()
    : f_error_count(error::instance().get_error_count())
    , f_warning_count(error::instance().get_warning_count())
{
}

safe_error_t::~safe_error_t()
{
    error::instance().set_error_count(f_error_count);
    error::instance().set_warning_count(f_warning_count);
}

safe_error_stream_t::safe_error_stream_t(std::ostream & err_stream)
    : f_error(&error::instance().get_error_stream())
{
    error::instance().set_error_stream(err_stream);
}

safe_error_stream_t::~safe_error_stream_t()
{
    error::instance().set_error_stream(*f_error);
}

error_happened_t::error_happened_t()
    : f_error_count(error::instance().get_error_count())
    , f_warning_count(error::instance().get_warning_count())
{
}

bool error_happened_t::error_happened() const
{
    return f_error_count != error::instance().get_error_count();
}

bool error_happened_t::warning_happened() const
{
    return f_warning_count != error::instance().get_warning_count();
}

} // namespace csspp

std::ostream & operator << (std::ostream & out, csspp::error_mode_t const type)
{
    switch(type)
    {
    case csspp::error_mode_t::ERROR_DEBUG:
        out << "debug";
        break;

    case csspp::error_mode_t::ERROR_DEC:
        out << "dec";
        break;

    case csspp::error_mode_t::ERROR_ERROR:
        out << "error";
        break;

    case csspp::error_mode_t::ERROR_FATAL:
        out << "fatal";
        break;

    case csspp::error_mode_t::ERROR_INFO:
        out << "info";
        break;

    case csspp::error_mode_t::ERROR_HEX:
        out << "hex";
        break;

    case csspp::error_mode_t::ERROR_WARNING:
        out << "warning";
        break;

    }

    return out;
}

// Local Variables:
// mode: cpp
// indent-tabs-mode: nil
// c-basic-offset: 4
// tab-width: 4
// End:

// vim: ts=4 sw=4 et
