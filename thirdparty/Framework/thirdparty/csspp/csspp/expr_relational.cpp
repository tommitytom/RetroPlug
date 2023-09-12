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

#include    "csspp/exception.h"
#include    "csspp/parser.h"
#include    "csspp/unicode_range.h"

#include    <algorithm>
#include    <cmath>
#include    <iostream>

namespace csspp
{

bool expression::is_less_than(node::pointer_t lhs, node::pointer_t rhs)
{
    switch(mix_node_types(lhs->get_type(), rhs->get_type()))
    {
    case mix_node_types(node_type_t::BOOLEAN, node_type_t::BOOLEAN):
        return lhs->get_boolean() < rhs->get_boolean();

    case mix_node_types(node_type_t::INTEGER, node_type_t::INTEGER):
        // TBD: should we generate an error if these are not
        //      equivalent dimensions?
        return lhs->get_integer() < rhs->get_integer();

    case mix_node_types(node_type_t::INTEGER, node_type_t::DECIMAL_NUMBER):
        // TBD: should we generate an error if these are not
        //      equivalent dimensions?
        return lhs->get_integer() < rhs->get_decimal_number();

    case mix_node_types(node_type_t::DECIMAL_NUMBER, node_type_t::INTEGER):
        // TBD: should we generate an error if these are not
        //      equivalent dimensions?
        return lhs->get_decimal_number() < rhs->get_integer();

    case mix_node_types(node_type_t::DECIMAL_NUMBER, node_type_t::DECIMAL_NUMBER):
        // TBD: should we generate an error if these are not
        //      equivalent dimensions?
        return lhs->get_decimal_number() < rhs->get_decimal_number();

    case mix_node_types(node_type_t::PERCENT, node_type_t::PERCENT):
        return lhs->get_decimal_number() < rhs->get_decimal_number();

    case mix_node_types(node_type_t::STRING, node_type_t::STRING):
        return lhs->get_string() < rhs->get_string();

    }

    // at this time this only really applies to 'COLOR op COLOR'
    error::instance() << lhs->get_position()
            << "incompatible types between "
            << lhs->get_type()
            << " and "
            << rhs->get_type()
            << " for operator '<', '<=', '>', or '>='."
            << error_mode_t::ERROR_ERROR;

    return false;
}

node_type_t relational_operator(node::pointer_t n)
{
    switch(n->get_type())
    {
    case node_type_t::LESS_THAN:
    case node_type_t::LESS_EQUAL:
    case node_type_t::GREATER_THAN:
    case node_type_t::GREATER_EQUAL:
        return n->get_type();

    default:
        return node_type_t::UNKNOWN;

    }
}

node::pointer_t expression::relational()
{
    // relational: additive
    //           | relational '<' additive
    //           | relational '<=' additive
    //           | relational '>' additive
    //           | relational '>=' additive

    node::pointer_t result(additive());
    if(!result)
    {
        return node::pointer_t();
    }

    node_type_t op(relational_operator(f_current));
    while(op != node_type_t::UNKNOWN)
    {
        position pos(f_current->get_position());

        // skip the relational operator
        next();

        node::pointer_t rhs(additive());
        if(!rhs)
        {
            return node::pointer_t();
        }

        // if not comparable, go on, although we already generated an
        // error; but at least the rest of the expression can be
        // parsed properly
        if(is_comparable(result, rhs))
        {
            // apply the equality operation
            bool boolean_result(false);
            switch(op)
            {
            case node_type_t::LESS_THAN:
                boolean_result = is_less_than(result, rhs);
                break;

            case node_type_t::LESS_EQUAL:
                boolean_result = is_less_than(result, rhs) || is_equal(result, rhs);
                break;

            case node_type_t::GREATER_THAN:
                boolean_result = !is_less_than(result, rhs) && !is_equal(result, rhs);
                break;

            case node_type_t::GREATER_EQUAL:
                boolean_result = !is_less_than(result, rhs);
                break;

            default:
                throw csspp_exception_logic("expression.cpp:relational(): unexpected operator in 'op'."); // LCOV_EXCL_LINE

            }
            result.reset(new node(node_type_t::BOOLEAN, pos));
            result->set_boolean(boolean_result);
        }

        op = relational_operator(f_current);
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
