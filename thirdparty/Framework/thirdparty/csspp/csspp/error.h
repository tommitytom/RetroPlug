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
#pragma once

// self
//
#include    "csspp/position.h"


// C++ lib
//
#include    <istream>
#include    <sstream>


namespace csspp
{

enum class error_mode_t
{
    ERROR_DEBUG,
    ERROR_DEC,
    ERROR_ERROR,
    ERROR_FATAL,
    ERROR_HEX,
    ERROR_INFO,
    ERROR_WARNING
};

typedef uint32_t            error_count_t;

// the bare pointer is problematic with effective c++
//#pragma GCC diagnostic push
//#pragma GCC diagnostic ignored "-Weffc++"
class error
{
public:
                            error();

    static error &          instance();
    std::ostream &          get_error_stream() const;
    void                    set_error_stream(std::ostream & err_stream);

    void                    set_count_warnings_as_errors(bool warnings_as_errors);

    error_count_t           get_error_count() const;
    void                    set_error_count(error_count_t count);
    error_count_t           get_warning_count() const;
    void                    set_warning_count(error_count_t count);

    void                    set_hide_all(bool show_debug);
    void                    set_show_debug(bool show_debug);

    void                    set_verbose(bool status);

    error &                 operator << (position const & pos);
    error &                 operator << (error_mode_t mode);
    error &                 operator << (std::string const & msg);
    error &                 operator << (char const * msg);
    error &                 operator << (int32_t value);
    error &                 operator << (int64_t value);
    error &                 operator << (double value);

private:
    void                    reset();

    position                f_position;
    std::stringstream       f_message = std::stringstream();
    std::ostream *          f_error = nullptr;
    error_count_t           f_error_count = 0;
    error_count_t           f_warning_count = 0;
    bool                    f_warnings_as_errors = false;
    bool                    f_hide_all = false;
    bool                    f_show_debug = false;
    bool                    f_verbose = false;
};
//#pragma GCC diagnostic pop

class safe_error_t
{
public:
    safe_error_t();
    ~safe_error_t();

private:
    error_count_t           f_error_count = 0;
    error_count_t           f_warning_count = 0;
};


class safe_error_stream_t
{
public:
                            safe_error_stream_t(std::ostream & err_stream);
                            safe_error_stream_t(safe_error_stream_t const & rhs) = delete;
                            ~safe_error_stream_t();

    safe_error_stream_t &   operator = (safe_error_stream_t const & rhs) = delete;

private:
    std::ostream *          f_error = nullptr;
};


class error_happened_t
{
public:
                            error_happened_t();

    bool                    error_happened() const;
    bool                    warning_happened() const;

private:
    error_count_t           f_error_count = 0;
    error_count_t           f_warning_count = 0;
};

} // namespace csspp

std::ostream & operator << (std::ostream & out, csspp::error_mode_t const type);

// vim: ts=4 sw=4 et
