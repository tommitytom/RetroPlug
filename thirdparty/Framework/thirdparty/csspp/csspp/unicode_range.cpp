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

#include    "csspp/unicode_range.h"

#include    "csspp/exception.h"

#include    <iomanip>
#include    <iostream>
#include    <sstream>

namespace csspp
{

namespace
{

wide_char_t constexpr range_to_start(range_value_t const range)
{
    return range & 0xFFFFFFFFLL;
}

wide_char_t constexpr range_to_end(range_value_t const range)
{
    return (range >> 32) & 0xFFFFFFFFLL;
}

range_value_t constexpr start_end_to_range(wide_char_t start, wide_char_t end)
{
    return (static_cast<range_value_t>(end) << 32) | static_cast<range_value_t>(start);
}

void verify_range(range_value_t const range)
{
    // verify maximum
    wide_char_t const start(range_to_start(range));
    if(start >= 0x110000)
    {
        std::stringstream ss;
        ss << "the start parameter is limited to a maximum of 0x10FFFF, it is 0x" << std::hex << start << " now.";
        throw csspp_exception_overflow(ss.str());
    }

    // verify minimum
    wide_char_t const end(range_to_end(range));
    if(end >= 0x200000) // special end to support all possible masks
    {
        std::stringstream ss;
        ss << "the end parameter is limited to a maximum of 0x1FFFFF, it is 0x" << std::hex << end << " now.";
        throw csspp_exception_overflow(ss.str());
    }

    // must always be properly ordered
    if(start > end)
    {
        std::stringstream ss;
        ss << "the start parameter (" << std::hex << start << ") cannot be larged than the end parameter (" << end << ") in a unicode range.";
        throw csspp_exception_overflow(ss.str());
    }
}

} // no name namespace

unicode_range_t::unicode_range_t(range_value_t range)
    : f_range(range)
{
    verify_range(f_range);
}

unicode_range_t::unicode_range_t(wide_char_t start, wide_char_t end)
    : f_range(start_end_to_range(start, end))
{
    verify_range(f_range);
}

void unicode_range_t::set_range(range_value_t range)
{
    verify_range(range);
    f_range = range;
}

void unicode_range_t::set_range(wide_char_t start, wide_char_t end)
{
    range_value_t const range(start_end_to_range(start, end));
    verify_range(range);
    f_range = range;
}

range_value_t unicode_range_t::get_range() const
{
    return f_range;
}

wide_char_t unicode_range_t::get_start() const
{
    return range_to_start(f_range);
}

wide_char_t unicode_range_t::get_end() const
{
    return range_to_end(f_range);
}

std::string unicode_range_t::to_string() const
{
    wide_char_t start(range_to_start(f_range));
    wide_char_t end(range_to_end(f_range));

    // a special case for the 6 x '?' mask
    if(start == 0 && end >= 0x10FFFF)
    {
        return "??????";
    }

    std::stringstream ss;
    ss << std::hex << start;

    // if start and end are equal
    if(start == end)
    {
        // if equal, we return that one number
        return ss.str();
    }

    // check whether we can use the question mark trick
    {
        std::stringstream filled;
        filled << std::hex << std::setw(6) << std::setfill('0') << start;
        std::string const start_str(filled.str());
        filled.str("");
        filled << std::setw(6) << end;
        std::string const end_str(filled.str());
        if(start_str.length() != 6 || end_str.length() != 6)
        {
            throw csspp_exception_logic("unexpected string length"); // LCOV_EXCL_LINE
        }
        size_t p(6);
        for(; p > 0; --p)
        {
            if(start_str[p - 1] != '0' || end_str[p - 1] != 'f')
            {
                break;
            }
        }
        std::string result(start_str.substr(0, p));
        if(result == end_str.substr(0, p))
        {
            // we can use a plain ??? mask
            result += std::string("??????", 6 - p);
            // remove leading zeroes
            while(result.front() == '0')
            {
                result.erase(result.begin());
            }
            return result;
        }
    }

    // no question mark, just append the end
    ss << "-" << end;

    return ss.str();
}

} // namespace csspp

// Local Variables:
// mode: cpp
// indent-tabs-mode: nil
// c-basic-offset: 4
// tab-width: 4
// End:

// vim: ts=4 sw=4 et
