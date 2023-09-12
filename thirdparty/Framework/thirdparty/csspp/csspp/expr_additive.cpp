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

// self
//
#include    "csspp/expression.h"

#include    "csspp/exception.h"
#include    "csspp/parser.h"
#include    "csspp/unicode_range.h"


// C++
//
#include    <algorithm>
#include    <cmath>
#include    <iostream>

// last include
//
////#include    <snapdev/poison.h>



namespace csspp
{

namespace
{

node_type_t additive_operator(node::pointer_t n)
{
    switch(n->get_type())
    {
    case node_type_t::ADD:
    case node_type_t::SUBTRACT:
        return n->get_type();

    default:
        return node_type_t::UNKNOWN;

    }
}

node::pointer_t add(node::pointer_t lhs, node::pointer_t rhs, bool subtract)
{
    node_type_t type(node_type_t::UNKNOWN);
    bool test_dimensions(true);
    integer_t ai(0);
    integer_t bi(0);
    decimal_number_t af(0.0);
    decimal_number_t bf(0.0);
    bool swapped(false);

    switch(mix_node_types(lhs->get_type(), rhs->get_type()))
    {
    case mix_node_types(node_type_t::STRING, node_type_t::STRING):
        if(!subtract)
        {
            // string concatenation
            node::pointer_t result(new node(node_type_t::STRING, lhs->get_position()));
            result->set_string(lhs->get_string() + rhs->get_string());
            return result;
        }
        break;

    case mix_node_types(node_type_t::INTEGER, node_type_t::INTEGER):
        ai = lhs->get_integer();
        bi = rhs->get_integer();
        type = node_type_t::INTEGER;
        break;

    case mix_node_types(node_type_t::DECIMAL_NUMBER, node_type_t::DECIMAL_NUMBER):
        af = lhs->get_decimal_number();
        bf = rhs->get_decimal_number();
        type = node_type_t::DECIMAL_NUMBER;
        break;

    case mix_node_types(node_type_t::DECIMAL_NUMBER, node_type_t::INTEGER):
        af = lhs->get_decimal_number();
        bf = static_cast<decimal_number_t>(rhs->get_integer());
        type = node_type_t::DECIMAL_NUMBER;
        break;

    case mix_node_types(node_type_t::INTEGER, node_type_t::DECIMAL_NUMBER):
        af = static_cast<decimal_number_t>(lhs->get_integer());
        bf = rhs->get_decimal_number();
        type = node_type_t::DECIMAL_NUMBER;
        break;

    case mix_node_types(node_type_t::PERCENT, node_type_t::PERCENT):
        af = lhs->get_decimal_number();
        bf = rhs->get_decimal_number();
        type = node_type_t::PERCENT;
        test_dimensions = false;
        break;

    case mix_node_types(node_type_t::INTEGER, node_type_t::COLOR):
    case mix_node_types(node_type_t::DECIMAL_NUMBER, node_type_t::COLOR):
        swap(lhs, rhs);
        swapped = true;
#if __cplusplus >= 201700
        [[fallthrough]];
#endif
    case mix_node_types(node_type_t::COLOR, node_type_t::INTEGER):
    case mix_node_types(node_type_t::COLOR, node_type_t::DECIMAL_NUMBER):
        if(rhs->get_string() == "")
        {
            decimal_number_t offset;
            if(rhs->is(node_type_t::INTEGER))
            {
                offset = static_cast<decimal_number_t>(rhs->get_integer());
            }
            else
            {
                offset = rhs->get_decimal_number();
            }
            color c(lhs->get_color());
            color_component_t red;
            color_component_t green;
            color_component_t blue;
            color_component_t alpha;
            c.get_color(red, green, blue, alpha);
            if(subtract)
            {
                if(swapped)
                {
                    red   = offset - red;
                    green = offset - green;
                    blue  = offset - blue;
                    alpha = offset - alpha;
                }
                else
                {
                    red   -= offset;
                    green -= offset;
                    blue  -= offset;
                    alpha -= offset;
                }
            }
            else
            {
                red   += offset;
                green += offset;
                blue  += offset;
                alpha += offset;
            }
            c.set_color(red, green, blue, alpha);
            node::pointer_t result(new node(node_type_t::COLOR, lhs->get_position()));
            result->set_color(c);
            return result;
        }
        error::instance() << rhs->get_position()
                << "color offsets (numbers added with + or - operators) must be unit less values, "
                << (rhs->is(node_type_t::INTEGER)
                            ? static_cast<decimal_number_t>(rhs->get_integer())
                            : rhs->get_decimal_number())
                << rhs->get_string()
                << " is not acceptable."
                << error_mode_t::ERROR_ERROR;
        return node::pointer_t();

    case mix_node_types(node_type_t::COLOR, node_type_t::COLOR):
        {
            color lc(lhs->get_color());
            color const rc(rhs->get_color());
            color_component_t lred;
            color_component_t lgreen;
            color_component_t lblue;
            color_component_t lalpha;
            color_component_t rred;
            color_component_t rgreen;
            color_component_t rblue;
            color_component_t ralpha;
            lc.get_color(lred, lgreen, lblue, lalpha);
            rc.get_color(rred, rgreen, rblue, ralpha);
            if(subtract)
            {
                lred   -= rred;
                lgreen -= rgreen;
                lblue  -= rblue;
                lalpha -= ralpha;
            }
            else
            {
                lred   += rred;
                lgreen += rgreen;
                lblue  += rblue;
                lalpha += ralpha;
            }
            lc.set_color(lred, lgreen, lblue, lalpha);
            node::pointer_t result(new node(node_type_t::COLOR, lhs->get_position()));
            result->set_color(lc);
            return result;
        }

    default:
        break;

    }

    if(type == node_type_t::UNKNOWN)
    {
        node_type_t lt(lhs->get_type());
        node_type_t rt(rhs->get_type());

        error::instance() << lhs->get_position()
                << "incompatible types between "
                << lt
                << (lt == node_type_t::IDENTIFIER || lt == node_type_t::STRING ? " (" + lhs->get_string() + ")" : "")
                << " and "
                << rt
                << (rt == node_type_t::IDENTIFIER || rt == node_type_t::STRING ? " (" + rhs->get_string() + ")" : "")
                << " for operator '"
                << (subtract ? "-" : "+")
                << "'."
                << error_mode_t::ERROR_ERROR;
        return node::pointer_t();
    }

    if(test_dimensions)
    {
        std::string const ldim(lhs->get_string());
        std::string const rdim(rhs->get_string());
        if(ldim != rdim)
        {
            error::instance() << lhs->get_position()
                    << "incompatible dimensions: \""
                    << ldim
                    << "\" and \""
                    << rdim
                    << "\" cannot be used as is with operator '"
                    << (subtract ? "-" : "+")
                    << "'."
                    << error_mode_t::ERROR_ERROR;
            return node::pointer_t();
        }
    }

    node::pointer_t result(new node(type, lhs->get_position()));
    if(type != node_type_t::PERCENT)
    {
        // do not lose the dimension
        result->set_string(lhs->get_string());
    }

    switch(type)
    {
    case node_type_t::INTEGER:
        if(subtract)
        {
            result->set_integer(ai - bi);
        }
        else
        {
            result->set_integer(ai + bi);
        }
        break;

    case node_type_t::DECIMAL_NUMBER:
    case node_type_t::PERCENT:
        if(subtract)
        {
            result->set_decimal_number(af - bf);
        }
        else
        {
            result->set_decimal_number(af + bf);
        }
        break;

    default:
        throw csspp_exception_logic("expression.cpp:add(): 'type' set to a value which is not handled here."); // LCOV_EXCL_LINE

    }

    return result;
}

} // no name namespace

node::pointer_t expression::additive()
{
    //  additive: multiplicative
    //          | additive '+' multiplicative
    //          | additive '-' multiplicative

    node::pointer_t result(multiplicative());
    if(!result)
    {
        return node::pointer_t();
    }

    node_type_t op(additive_operator(f_current));
    while(op != node_type_t::UNKNOWN)
    {
        // skip the additive operator
        next();

        node::pointer_t rhs(multiplicative());
        if(!rhs)
        {
            return node::pointer_t();
        }

        // apply the additive operation
        result = add(result, rhs, op == node_type_t::SUBTRACT);
        if(!result)
        {
            return node::pointer_t();
        }

        op = additive_operator(f_current);
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
