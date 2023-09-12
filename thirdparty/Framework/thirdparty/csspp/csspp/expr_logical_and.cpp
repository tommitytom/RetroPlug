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

/** \brief Check whether a node represents true or false.
 *
 * This function checks whether a node represents a valid boolean. If
 * so it returns true or false depending on that fact.
 *
 * If the node does not represent a boolean, then the function returns
 * false after it generated an error.
 *
 * \note
 * The function is also used by the conditional() and logical_or()
 * functions.
 *
 * \param[in] n  The node to check whether it is true or false.
 *
 * \return true or false.
 */
bool expression::boolean(node::pointer_t n)
{
    boolean_t const result(n->to_boolean());
    if(result == boolean_t::BOOLEAN_INVALID)
    {
        error::instance() << n->get_position()
                << "a boolean expression was expected."
                << error_mode_t::ERROR_ERROR;
    }
    return result == boolean_t::BOOLEAN_TRUE;
}

node::pointer_t expression::logical_and()
{
    // logical_and: equality
    //            | logical_and IDENTIFIER (='and') equality
    //            | logical_and '&&' equality

    node::pointer_t result(equality());
    if(!result)
    {
        return node::pointer_t();
    }

    while((f_current->is(node_type_t::IDENTIFIER) && f_current->get_string() == "and")
       || f_current->is(node_type_t::AND))
    {
        position pos(f_current->get_position());

        // skip the AND
        next();

        node::pointer_t rhs(equality());
        if(!rhs)
        {
            return node::pointer_t();
        }

        // apply the AND
        bool const lr(boolean(result));
        bool const rr(boolean(rhs));
        result.reset(new node(node_type_t::BOOLEAN, pos));
        result->set_boolean(lr && rr);
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
