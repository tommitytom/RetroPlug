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
 * \brief Implementation of the CSS Preprocessor expression.
 *
 * The CSS Preprocessor expression class is used to reduce a list
 * of nodes by applying expressions to the various values.
 *
 * \sa \ref expression_rules
 */

#include    "csspp/expression.h"

#include    "csspp/parser.h"
#include    "csspp/unicode_range.h"

#include    <algorithm>
#include    <cmath>
#include    <iostream>

namespace csspp
{

node::pointer_t expression::conditional()
{
    // conditional: logical_or
    //            | conditional '?' conditional ':' logical_or

    // note: we also support if(expr, expr, expr)

    node::pointer_t result(logical_or());
    if(!result)
    {
        return node::pointer_t();
    }

    while(f_current->is(node_type_t::CONDITIONAL))
    {
        bool const r(boolean(result));

        // skip the '?'
        next();

        // TODO: avoid calculating both sides.
        if(r)
        {
            result = conditional();
            if(!result)
            {
                return node::pointer_t();
            }
        }
        else
        {
            // we will ignore this entry, so ignore the eventual errors
            csspp::safe_error_t safe_errors;
            std::stringstream ignore;
            safe_error_stream_t safe_output(ignore);

            static_cast<void>(conditional());
        }

        if(!f_current->is(node_type_t::COLON))
        {
            error::instance() << f_current->get_position()
                    << "a mandatory ':' was expected after a '?' first expression."
                    << error_mode_t::ERROR_ERROR;
            return node::pointer_t();
        }

        // skip the ':'
        next();

        if(!r)
        {
            result = conditional();
            if(!result)
            {
                return node::pointer_t();
            }
        }
        else
        {
            // we will ignore this entry, so ignore the eventual errors
            csspp::safe_error_t safe_errors;
            std::stringstream ignore;
            safe_error_stream_t safe_output(ignore);

            static_cast<void>(conditional());
        }
    }

    return result;
}

} // namespace csspp

// Local Variables:
// mode: cpp
// indent-tabs-mode: nil
// c-basic-offset: 4
// tab-width: 4
// End:

// vim: ts=4 sw=4 et
