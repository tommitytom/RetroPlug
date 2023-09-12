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
#include    <csspp/csspp.h>

namespace csspp
{

typedef uint64_t        range_value_t;

class unicode_range_t
{
public:
                    unicode_range_t(range_value_t value = 0);
                    unicode_range_t(wide_char_t start, wide_char_t end);

    void            set_range(range_value_t range);
    void            set_range(wide_char_t start, wide_char_t end);

    range_value_t   get_range() const;
    wide_char_t     get_start() const;
    wide_char_t     get_end() const;

    std::string     to_string() const;

private:
    range_value_t   f_range = 0;
};

} // namespace csspp
// vim: ts=4 sw=4 et
