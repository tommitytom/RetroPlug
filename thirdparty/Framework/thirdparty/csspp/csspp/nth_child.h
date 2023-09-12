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
#include    "csspp/csspp.h"


namespace csspp
{

typedef int32_t             repeat_integer_t;

class nth_child
{
public:
                            nth_child(integer_t an_plus_b = 1);
                            nth_child(repeat_integer_t a, repeat_integer_t b);
                            nth_child(std::string const & an_plus_b);

    void                    set_a(repeat_integer_t a);
    void                    set_b(repeat_integer_t b);
    repeat_integer_t        get_a() const;
    repeat_integer_t        get_b() const;
    integer_t               get_nth() const;
    std::string             get_error() const;

    bool                    parse(std::string const & an_plus_b);

    std::string             to_string() const;

private:
    std::string             f_error = std::string();
    repeat_integer_t        f_a = 1;
    repeat_integer_t        f_b = 0;
};

} // namespace csspp
// vim: ts=4 sw=4 et
