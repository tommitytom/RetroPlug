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

node::pointer_t expression::apply_power(node::pointer_t lhs, node::pointer_t rhs)
{
    node::pointer_t result;

    if(rhs->is(node_type_t::INTEGER)
    || rhs->is(node_type_t::DECIMAL_NUMBER))
    {
        if(rhs->get_string() != "")
        {
            error::instance() << f_current->get_position()
                    << "the number representing the power cannot be a dimension ("
                    << rhs->get_string()
                    << "); it has to be unitless."
                    << error_mode_t::ERROR_ERROR;
            return node::pointer_t();
        }
    }

    bool check_dimension(false);
    switch(mix_node_types(lhs->get_type(), rhs->get_type()))
    {
    case mix_node_types(node_type_t::INTEGER, node_type_t::INTEGER):
        result.reset(new node(node_type_t::INTEGER, lhs->get_position()));
        result->set_integer(static_cast<integer_t>(pow(lhs->get_integer(), rhs->get_integer())));
        check_dimension = true;
        break;

    case mix_node_types(node_type_t::INTEGER, node_type_t::DECIMAL_NUMBER):
        result.reset(new node(node_type_t::DECIMAL_NUMBER, lhs->get_position()));
        result->set_decimal_number(pow(lhs->get_integer(), rhs->get_decimal_number()));
        check_dimension = true;
        break;

    case mix_node_types(node_type_t::DECIMAL_NUMBER, node_type_t::INTEGER):
        result.reset(new node(node_type_t::DECIMAL_NUMBER, lhs->get_position()));
        result->set_decimal_number(pow(lhs->get_decimal_number(), rhs->get_integer()));
        check_dimension = true;
        break;

    case mix_node_types(node_type_t::DECIMAL_NUMBER, node_type_t::DECIMAL_NUMBER):
        result.reset(new node(node_type_t::DECIMAL_NUMBER, lhs->get_position()));
        result->set_decimal_number(pow(lhs->get_decimal_number(), rhs->get_decimal_number()));
        check_dimension = true;
        break;

    case mix_node_types(node_type_t::PERCENT, node_type_t::INTEGER):
        result.reset(new node(node_type_t::PERCENT, lhs->get_position()));
        result->set_decimal_number(pow(lhs->get_decimal_number(), rhs->get_integer()));
        break;

    case mix_node_types(node_type_t::PERCENT, node_type_t::DECIMAL_NUMBER):
        result.reset(new node(node_type_t::PERCENT, lhs->get_position()));
        result->set_decimal_number(pow(lhs->get_decimal_number(), rhs->get_decimal_number()));
        break;

    default:
        error::instance() << f_current->get_position()
                << "incompatible types between "
                << lhs->get_type()
                << " and "
                << rhs->get_type()
                << " for operator '**'."
                << error_mode_t::ERROR_ERROR;
        return node::pointer_t();

    }

    if(check_dimension
    && lhs->get_string() != "")
    {
        integer_t the_power(0);
        if(rhs->is(node_type_t::INTEGER))
        {
            // integers are fine if > 0
            the_power = rhs->get_integer();
        }
        else
        {
            decimal_number_t p(rhs->get_decimal_number());
            decimal_number_t integral_part(0.0);
            decimal_number_t fractional_part(modf(p, &integral_part));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
            if(fractional_part != 0.0)
#pragma GCC diagnostic pop
            {
                // the fractional part has to be exactly 0.0 otherwise we
                // cannot determine the new dimension
                error::instance() << f_current->get_position()
                        << "a number with a dimension only supports integers as their power (i.e. 3px ** 2 is fine, 3px ** 2.1 is not supported)."
                        << error_mode_t::ERROR_ERROR;
                return node::pointer_t();
            }
            the_power = static_cast<integer_t>(integral_part);
        }
        if(the_power == 0)
        {
            error::instance() << f_current->get_position()
                    << "a number with a dimension power zero cannot be calculated (i.e. 3px ** 0 = 1 what?)."
                    << error_mode_t::ERROR_ERROR;
            return node::pointer_t();
        }
        // impose a limit because otherwise we may have a bit of a memory
        // problem...
        if(labs(the_power) > 100)
        {
            error::instance() << f_current->get_position()
                    << "a number with a dimension power 101 or more would generate a very large string so we refuse it at this time. You may use unitless numbers instead."
                    << error_mode_t::ERROR_ERROR;
            return node::pointer_t();
        }

        // calculate the new dimension, if power is negative, make sure
        // to swap the existing dimension (i.e. px / em -> em / px)
        dimension_vector_t org_dividend;
        dimension_vector_t org_divisor;
        dimension_vector_t dividend;
        dimension_vector_t divisor;

        if(the_power >= 0)
        {
            dimensions_to_vectors(lhs->get_position(), lhs->get_string(), org_dividend, org_divisor);
        }
        else
        {
            dimensions_to_vectors(lhs->get_position(), lhs->get_string(), org_divisor, org_dividend);
        }

        the_power = labs(the_power);
        for(integer_t idx(0); idx < the_power; ++idx)
        {
            for(auto d : org_dividend)
            {
                dividend.push_back(d);
            }
            for(auto d : org_divisor)
            {
                divisor.push_back(d);
            }
        }

        result->set_string(rebuild_dimension(dividend, divisor));
    }

    return result;
}

node::pointer_t expression::power()
{
    // power: post
    //      | post '**' post

    // no loop because we do not allow 'a ** b ** c'
    node::pointer_t result(post());
    if(result
    && ((f_current->is(node_type_t::IDENTIFIER) && f_current->get_string() == "pow")
      || f_current->is(node_type_t::POWER)))
    {
        // skip the power operator
        next();

        node::pointer_t rhs(post());
        if(rhs == nullptr)
        {
            return node::pointer_t();
        }

        // apply the power operation
        result = apply_power(result, rhs);
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
