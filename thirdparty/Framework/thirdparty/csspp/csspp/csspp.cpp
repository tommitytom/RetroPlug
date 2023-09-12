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
 * \brief Implementation of the CSS Preprocessor library.
 *
 * This file represents the main implementation of the CSS Preprocessor
 * library. It has only a few functions handling code that's common
 * to all the other classes. It also has the library version information.
 *
 * \sa \ref lexer_rules
 */

// self
//
#include    "csspp/csspp.h"

#include    "csspp/exception.h"


// C++
//
#include    <cmath>
#include    <cfloat>
#include    <iomanip>
#include    <sstream>
#include    <iostream>


// last include
//
//#include    <snapdev/poison.h>



/** \brief The namespace of all the classes in the CSS Preprocessor.
 *
 * All the classes and many definitions appear under 'csspp'. It
 * is strongly advised that you do not do a 'using csspp;' because
 * some of the definitions are likely to spoil your namespace in
 * a way you do not want it to.
 */
namespace csspp
{

namespace
{

int g_precision = 3;

} // no name namespace

char const * csspp_library_version()
{
    return CSSPP_VERSION;
}

int get_precision()
{
    return g_precision;
}

void set_precision(int precision)
{
    if(precision < 0 || precision > 10)
    {
        throw csspp_exception_overflow("precision must be between 0 and 10, " + std::to_string(precision) + " is out of bounds.");
    }

    g_precision = precision;
}

std::string decimal_number_to_string(decimal_number_t d, bool remove_leading_zero)
{
    // the std::setprecision() is a total number of digits when we
    // want a specific number of digits after the decimal point so
    // we use the following algorithm for it:
    std::stringstream ss;

    // use the maximum precision so we do not get any surprises
    // see the following for the "3 + DBL_MANT_DIG - DBL_MIN_EXP":
    // http://stackoverflow.com/questions/1701055/what-is-the-maximum-length-in-chars-needed-to-represent-any-double-value
    ss << std::setprecision(3 + DBL_MANT_DIG - DBL_MIN_EXP);

    // make sure to round the value up first
    if(d >= 0.0)
    {
        ss << d + 0.5 / pow(10.0, g_precision);
    }
    else
    {
        ss << d - 0.5 / pow(10.0, g_precision);
    }

    std::string out(ss.str());

    // check wether the number of digits after the decimal point is too large
    std::string::size_type end(out.find('.'));
    if(end != std::string::npos
    && out.length() > end + g_precision + 1)
    {
        // remove anything extra
        out = out.substr(0, end + g_precision + 1);
    }
    if(out.find('.') != std::string::npos
    && end != std::string::npos)
    {
        while(out.back() == '0')
        {
            out.erase(out.end() - 1);
        }
        if(out.back() == '.')
        {
            out.erase(out.end() - 1);
        }
    }

    // remove the leading zero when possible
    if(remove_leading_zero)
    {
        if(out.length() >= 3
        && out[0] == '0'
        && out[1] == '.')
        {
            out.erase(out.begin());
            // .33 is valid and equivalent to 0.33
        }
        else if(out.length() >= 4
             && out[0] == '-'
             && out[1] == '0'
             && out[2] == '.')
        {
            out.erase(out.begin() + 1);
            // -.33 is valid and equivalent to -0.33
        }
    }

    if(out == "-0")
    {
        // this happens with really small numbers because we lose
        // the precision and thus end up with zero even if the number
        // was not really zero
        return "0";
    }

    return out;
}

} // namespace csspp

// Local Variables:
// mode: cpp
// indent-tabs-mode: nil
// c-basic-offset: 4
// tab-width: 4
// End:

// vim: ts=4 sw=4 et
