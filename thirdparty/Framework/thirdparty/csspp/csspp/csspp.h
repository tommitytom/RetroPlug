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
#include    "csspp/exception.h"


// C++ lib
//
#include    <cstdint>
#include    <string>
#include    <sstream>


#ifdef __CYGWIN__
namespace std
{
    template<typename T>
    std::string to_string(T value)
    {
        std::stringstream ss;
        ss << value;
        return ss.str();
    }
}
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace csspp
{

#define CSSPP_VERSION_MAJOR    @CSSPP_VERSION_MAJOR@
#define CSSPP_VERSION_MINOR    @CSSPP_VERSION_MINOR@
#define CSSPP_VERSION_PATCH    @CSSPP_VERSION_PATCH@
#define CSSPP_VERSION          "@CSSPP_VERSION_MAJOR@.@CSSPP_VERSION_MINOR@.@CSSPP_VERSION_PATCH@"

typedef int32_t             wide_char_t;
typedef uint32_t            wide_uchar_t;
typedef int                 line_t;
typedef int64_t             integer_t;
typedef double              decimal_number_t;

char const *                csspp_library_version();
int                         get_precision();
void                        set_precision(int precision);
std::string                 decimal_number_to_string(decimal_number_t d, bool remove_leading_zero);

class safe_bool_t
{
public:
    safe_bool_t(bool & flag)
        : f_flag(flag)
        , f_old_value(flag)
    {
        f_flag = true;
    }

    safe_bool_t(bool & flag, bool new_value)
        : f_flag(flag)
        , f_old_value(flag)
    {
        f_flag = new_value;
    }

    ~safe_bool_t()
    {
        f_flag = f_old_value;
    }

private:
    bool &      f_flag;
    bool        f_old_value = false;
};

class safe_precision_t
{
public:
    safe_precision_t()
        : f_precision(get_precision())
    {
    }

    safe_precision_t(int precision)
        : f_precision(get_precision())
    {
        set_precision(precision);
    }

    ~safe_precision_t()
    {
        try
        {
            set_precision(f_precision);
        }
        catch(csspp_exception_overflow const &)
        {
        }
    }

private:
    int         f_precision = 0;
};

} // namespace csspp
// vim: ts=4 sw=4 et
