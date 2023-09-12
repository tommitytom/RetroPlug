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

#include    "csspp/position.h"

namespace csspp
{

position::position(std::string const & filename, line_t page, line_t line)
    : f_filename(filename)
    , f_page(page)
    , f_line(line)
    , f_total_line(line)
{
}

void position::next_line()
{
    ++f_line;
    ++f_total_line;
}

void position::next_page()
{
    ++f_page;
    f_line = 1;
}

std::string const & position::get_filename() const
{
    return f_filename;
}

line_t position::get_page() const
{
    return f_page;
}

line_t position::get_line() const
{
    return f_line;
}

line_t position::get_total_line() const
{
    return f_total_line;
}

} // namespace csspp

// Local Variables:
// mode: cpp
// indent-tabs-mode: nil
// c-basic-offset: 4
// tab-width: 4
// End:

// vim: ts=4 sw=4 et
