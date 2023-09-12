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

namespace
{

bool match(node_type_t op, node::pointer_t lhs, node::pointer_t rhs)
{
    std::string s;
    std::string l;

    switch(mix_node_types(lhs->get_type(), rhs->get_type()))
    {
    case mix_node_types(node_type_t::STRING, node_type_t::STRING):
        s = lhs->get_string();
        l = rhs->get_string();
        break;

    default:
        error::instance() << lhs->get_position()
                << "incompatible types between "
                << lhs->get_type()
                << " and "
                << rhs->get_type()
                << " for operator '~=', '^=', '$=', '*=', '|='."
                << error_mode_t::ERROR_ERROR;
        return lhs->get_string() == rhs->get_string();

    }

    switch(op)
    {
    case node_type_t::INCLUDE_MATCH:
        l = " " + l + " ";
        s = " " + s + " ";
        break;

    case node_type_t::PREFIX_MATCH:
        if(l.length() < s.length())
        {
            return false;
        }
        return s == l.substr(0, s.length());

    case node_type_t::SUFFIX_MATCH:
        if(l.length() < s.length())
        {
            return false;
        }
        return s == l.substr(l.length() - s.length());

    case node_type_t::SUBSTRING_MATCH:
        break;

    case node_type_t::DASH_MATCH:
        l = "-" + l + "-";
        s = "-" + s + "-";
        break;

    default:
        throw csspp_exception_logic("expression.cpp:include_match(): called with an invalid operator."); // LCOV_EXCL_LINE

    }

    return l.find(s) != std::string::npos;
}

node_type_t equality_operator(node::pointer_t n)
{
    switch(n->get_type())
    {
    // return type as is
    case node_type_t::EQUAL:
    case node_type_t::NOT_EQUAL:
    case node_type_t::INCLUDE_MATCH:
    case node_type_t::PREFIX_MATCH:
    case node_type_t::SUFFIX_MATCH:
    case node_type_t::SUBSTRING_MATCH:
    case node_type_t::DASH_MATCH:
        return n->get_type();

    case node_type_t::IDENTIFIER:
        {
            if(n->get_string() == "not-equal")
            {
                return node_type_t::NOT_EQUAL;
            }
        }
#if __cplusplus >= 201700
        [[fallthrough]];
#endif
    default:
        return node_type_t::UNKNOWN;

    }
}

} // no name namespace

bool expression::is_comparable(node::pointer_t lhs, node::pointer_t rhs)
{
    switch(mix_node_types(lhs->get_type(), rhs->get_type()))
    {
    case mix_node_types(node_type_t::BOOLEAN, node_type_t::BOOLEAN):
    case mix_node_types(node_type_t::COLOR, node_type_t::COLOR):
    case mix_node_types(node_type_t::PERCENT, node_type_t::PERCENT):
    case mix_node_types(node_type_t::STRING, node_type_t::STRING):
        return true;

    case mix_node_types(node_type_t::DECIMAL_NUMBER, node_type_t::DECIMAL_NUMBER):
    case mix_node_types(node_type_t::DECIMAL_NUMBER, node_type_t::INTEGER):
    case mix_node_types(node_type_t::INTEGER, node_type_t::DECIMAL_NUMBER):
    case mix_node_types(node_type_t::INTEGER, node_type_t::INTEGER):
        if(lhs->get_string() == rhs->get_string())
        {
            return true;
        }
        break;

    }

   // dimensions must be exactly the same or the comparison fails
   error::instance() << lhs->get_position()
        << "incompatible types or dimensions between "
        << lhs->get_type()
        << " and "
        << rhs->get_type()
        << " for operator '=', '!=', '<', '<=', '>', '>=', '~=', '^=', '$=', '*=', or '|='."
        << error_mode_t::ERROR_ERROR;

    return false;
}

bool expression::is_equal(node::pointer_t lhs, node::pointer_t rhs)
{
    switch(mix_node_types(lhs->get_type(), rhs->get_type()))
    {
    case mix_node_types(node_type_t::BOOLEAN, node_type_t::BOOLEAN):
        return lhs->get_boolean() == rhs->get_boolean();

    case mix_node_types(node_type_t::INTEGER, node_type_t::INTEGER):
        return lhs->get_integer() == rhs->get_integer();

    case mix_node_types(node_type_t::INTEGER, node_type_t::DECIMAL_NUMBER):
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
        return lhs->get_integer() == rhs->get_decimal_number();
#pragma GCC diagnostic pop

    case mix_node_types(node_type_t::DECIMAL_NUMBER, node_type_t::INTEGER):
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
        return lhs->get_decimal_number() == rhs->get_integer();
#pragma GCC diagnostic pop

    case mix_node_types(node_type_t::DECIMAL_NUMBER, node_type_t::DECIMAL_NUMBER):
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
        return lhs->get_decimal_number() == rhs->get_decimal_number();
#pragma GCC diagnostic pop

    case mix_node_types(node_type_t::PERCENT, node_type_t::PERCENT):
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
        return lhs->get_decimal_number() == rhs->get_decimal_number();
#pragma GCC diagnostic pop

    case mix_node_types(node_type_t::STRING, node_type_t::STRING):
        return lhs->get_string() == rhs->get_string();

    case mix_node_types(node_type_t::COLOR, node_type_t::COLOR):
        {
            color const lc(lhs->get_color());
            color const rc(rhs->get_color());
            return lc.get_color() == rc.get_color();
        }

    }

    // the is_comparable() function prevents us from reaching this line
    throw csspp_exception_logic("expression.cpp:include_match(): called with an invalid set of node types."); // LCOV_EXCL_LINE
}

node::pointer_t expression::equality()
{
    // equality: relational
    //         | equality '=' relational
    //         | equality '!=' relational
    //         | equality '~=' relational
    //         | equality '^=' relational
    //         | equality '$=' relational
    //         | equality '*=' relational
    //         | equality '|=' relational

    node::pointer_t result(relational());
    if(!result)
    {
        return node::pointer_t();
    }

    node_type_t op(equality_operator(f_current));
    while(op != node_type_t::UNKNOWN)
    {
        position pos(f_current->get_position());

        // skip the equality operator
        next();

        node::pointer_t rhs(relational());
        if(rhs == nullptr)
        {
            return node::pointer_t();
        }

        // apply the equality operation
        bool boolean_result(false);
        if(is_comparable(result, rhs))
        {
            switch(op)
            {
            case node_type_t::EQUAL:
                boolean_result = is_equal(result, rhs);
                break;

            case node_type_t::NOT_EQUAL:
                boolean_result = !is_equal(result, rhs);
                break;

            case node_type_t::INCLUDE_MATCH:
            case node_type_t::PREFIX_MATCH:
            case node_type_t::SUFFIX_MATCH:
            case node_type_t::SUBSTRING_MATCH:
            case node_type_t::DASH_MATCH:
                boolean_result = match(op, result, rhs);
                break;

            default:
                throw csspp_exception_logic("expression.cpp:equality(): unexpected operator in 'op'."); // LCOV_EXCL_LINE

            }
            result.reset(new node(node_type_t::BOOLEAN, pos));
            result->set_boolean(boolean_result);
        }

        op = equality_operator(f_current);
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
