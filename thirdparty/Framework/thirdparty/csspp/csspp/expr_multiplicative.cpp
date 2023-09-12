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
//#include    <snapdev/poison.h>



namespace csspp
{

node_type_t multiplicative_operator(node::pointer_t n)
{
    switch(n->get_type())
    {
    case node_type_t::IDENTIFIER:
        if(n->get_string() == "mul")
        {
            return node_type_t::MULTIPLY;
        }
        if(n->get_string() == "div")
        {
            return node_type_t::DIVIDE;
        }
        if(n->get_string() == "mod")
        {
            return node_type_t::MODULO;
        }
        break;

    case node_type_t::MULTIPLY:
    case node_type_t::DIVIDE:
    case node_type_t::MODULO:
        return n->get_type();

    default:
        break;

    }

    return node_type_t::UNKNOWN;
}

void expression::dimensions_to_vectors(position const & node_pos, std::string const & dimension, dimension_vector_t & dividend, dimension_vector_t & divisor)
{
    bool found_slash(false);
    std::string::size_type pos(0);

    // return early on empty otherwise we generate an error saying that
    // the dimension is missing (when unitless numbers are valid)
    if(dimension.empty())
    {
        return;
    }

    // we have a special case when there is no dividend in a dimension
    // this is defined as a "1" with a slash
    if(dimension.length() > 2
    && dimension.substr(0, 2) == "1/") // user defined may not include the space
    {
        pos = 2;
        found_slash = true;
    }
    else if(dimension.length() > 3
         && dimension.substr(0, 3) == "1 /")
    {
        pos = 3;
        found_slash = true;
    }

    // if it started with "1 /" then we may have yet another space: "1 / "
    if(found_slash
    && pos + 1 < dimension.size()
    && dimension[pos] == ' ')
    {
        ++pos;
    }

    for(;;)
    {
        {
            // get end of current dimension
            std::string::size_type end(dimension.find_first_of(" */", pos));
            if(end == std::string::npos)
            {
                end = dimension.size();
            }

            // add the dimension
            if(end != pos)
            {
                std::string const dim(dimension.substr(pos, end - pos));
                if(found_slash)
                {
                    divisor.push_back(dim);
                }
                else
                {
                    dividend.push_back(dim);
                }
            }
            else
            {
                error::instance() << node_pos
                        << "number dimension is missing a dimension name."
                        << error_mode_t::ERROR_ERROR;
                return;
            }

            pos = end;
            if(pos >= dimension.size())
            {
                return;
            }
        }

        // check the separator(s)
        if(pos < dimension.size()
        && dimension[pos] == ' ')
        {
            // this should always be true, but user defined separators
            // may not include the spaces
            ++pos;
        }
        if(pos >= dimension.size())
        {
            // a user dimension may end with a space, just ignore it
            return;
        }
        if(dimension[pos] == '/')
        {
            // second slash?!
            if(found_slash)
            {
                // user defined dimensions could have invalid dimension definitions
                error::instance() << node_pos
                        << "a valid dimension can have any number of '*' operators and a single '/' operator, here we found a second '/'."
                        << error_mode_t::ERROR_ERROR;
                return;
            }

            found_slash = true;
            ++pos;
        }
        else if(dimension[pos] == '*')
        {
            ++pos;
        }
        else
        {
            // what is that character?!
            error::instance() << node_pos
                    << "multiple dimensions can only be separated by '*' or '/' not '"
                    << dimension.substr(pos, 1)
                    << "'."
                    << error_mode_t::ERROR_ERROR;
            return;
        }
        if(pos < dimension.size()
        && dimension[pos] == ' ')
        {
            // this should always be true, but user defined separators
            // may not include the spaces
            ++pos;
        }
    }
}

std::string expression::multiplicative_dimension(position const & pos, std::string const & ldim, node_type_t const op, std::string const & rdim)
{
    dimension_vector_t dividend;
    dimension_vector_t divisor;

    // transform the string in one or two vectors
    dimensions_to_vectors(pos, ldim, dividend, divisor);

    if(op == node_type_t::MULTIPLY)
    {
        dimensions_to_vectors(pos, rdim, dividend, divisor);
    }
    else
    {
        // a division has the dividend / divisor inverted, simple trick
        dimensions_to_vectors(pos, rdim, divisor, dividend);
    }

    // optimize the result
    for(size_t idx(dividend.size()); idx > 0;)
    {
        --idx;
        std::string const dim(dividend[idx]);
        dimension_vector_t::iterator it(std::find(divisor.begin(), divisor.end(), dim));
        if(it != divisor.end())
        {
            // present in both places? if so remove from both places
            dividend.erase(dividend.begin() + idx);
            divisor.erase(it);
        }
    }

    // now we rebuild the resulting dimension
    // if the dividend is empty, then we have to put 1 / ...
    // if the divisor is empty, we do not put a '/ ...'

    return rebuild_dimension(dividend, divisor);
}

std::string expression::rebuild_dimension(dimension_vector_t const & dividend, dimension_vector_t const & divisor)
{
    std::string result;

    if(!dividend.empty() || !divisor.empty())
    {
        if(dividend.empty())
        {
            result += "1";
        }
        else
        {
            for(size_t idx(0); idx < dividend.size(); ++idx)
            {
                if(idx != 0)
                {
                    result += " * ";
                }
                result += dividend[idx];
            }
        }

        for(size_t idx(0); idx < divisor.size(); ++idx)
        {
            if(idx == 0)
            {
                result += " / ";
            }
            else
            {
                result += " * ";
            }
            result += divisor[idx];
        }
    }

    return result;
}

node::pointer_t expression::multiply(node_type_t op, node::pointer_t lhs, node::pointer_t rhs)
{
    node_type_t type(node_type_t::INTEGER);
    integer_t ai(0);
    integer_t bi(0);
    decimal_number_t af(0.0);
    decimal_number_t bf(0.0);

    switch(mix_node_types(lhs->get_type(), rhs->get_type()))
    {
    case mix_node_types(node_type_t::INTEGER, node_type_t::STRING):
        swap(lhs, rhs);
#if __cplusplus >= 201700
        [[fallthrough]];
#endif
    case mix_node_types(node_type_t::STRING, node_type_t::INTEGER):
        if(op != node_type_t::MULTIPLY)
        {
            error::instance() << lhs->get_position()
                    << "incompatible types between "
                    << lhs->get_type()
                    << " and "
                    << rhs->get_type()
                    << " for operator '/' or '%'."
                    << error_mode_t::ERROR_ERROR;
            return node::pointer_t();
        }
        else
        {
            integer_t count(rhs->get_integer());
            if(count < 0)
            {
                error::instance() << lhs->get_position()
                        << "string * integer requires that the integer not be negative ("
                        << rhs->get_integer()
                        << ")."
                        << error_mode_t::ERROR_ERROR;
                return node::pointer_t();
            }
            std::string result;
            for(; count > 0; --count)
            {
                result += lhs->get_string();
            }
            lhs->set_string(result);
        }
        return lhs;

    case mix_node_types(node_type_t::INTEGER, node_type_t::INTEGER):
        ai = lhs->get_integer();
        bi = rhs->get_integer();
        type = node_type_t::INTEGER;
        break;

    case mix_node_types(node_type_t::INTEGER, node_type_t::DECIMAL_NUMBER):
        af = static_cast<decimal_number_t>(lhs->get_integer());
        bf = rhs->get_decimal_number();
        type = node_type_t::DECIMAL_NUMBER;
        break;

    case mix_node_types(node_type_t::DECIMAL_NUMBER, node_type_t::INTEGER):
        af = lhs->get_decimal_number();
        bf = static_cast<decimal_number_t>(rhs->get_integer());
        type = node_type_t::DECIMAL_NUMBER;
        break;

    case mix_node_types(node_type_t::DECIMAL_NUMBER, node_type_t::PERCENT):
        af = lhs->get_decimal_number();
        bf = rhs->get_decimal_number();
        type = node_type_t::DECIMAL_NUMBER;
        break;

    case mix_node_types(node_type_t::PERCENT, node_type_t::DECIMAL_NUMBER):
        af = lhs->get_decimal_number();
        bf = rhs->get_decimal_number();
        type = node_type_t::DECIMAL_NUMBER;
        break;

    case mix_node_types(node_type_t::DECIMAL_NUMBER, node_type_t::DECIMAL_NUMBER):
        af = lhs->get_decimal_number();
        bf = rhs->get_decimal_number();
        type = node_type_t::DECIMAL_NUMBER;
        break;

    case mix_node_types(node_type_t::INTEGER, node_type_t::PERCENT):
        af = static_cast<decimal_number_t>(lhs->get_integer());
        bf = rhs->get_decimal_number();
        type = node_type_t::DECIMAL_NUMBER;
        break;

    case mix_node_types(node_type_t::PERCENT, node_type_t::INTEGER):
        af = lhs->get_decimal_number();
        bf = static_cast<decimal_number_t>(rhs->get_integer());
        type = node_type_t::DECIMAL_NUMBER;
        break;

    case mix_node_types(node_type_t::PERCENT, node_type_t::PERCENT):
        af = lhs->get_decimal_number();
        bf = rhs->get_decimal_number();
        type = node_type_t::PERCENT;
        break;

    case mix_node_types(node_type_t::NULL_TOKEN, node_type_t::NULL_TOKEN): // could this one support / and %?
    case mix_node_types(node_type_t::NULL_TOKEN, node_type_t::UNICODE_RANGE):
        if(op == node_type_t::MULTIPLY)
        {
            return lhs;
        }
        error::instance() << f_current->get_position()
                << "unicode_range * unicode_range is the only multiplicative operator accepted with unicode ranges, '/' and '%' are not allowed."
                << error_mode_t::ERROR_ERROR;
        return node::pointer_t();

    case mix_node_types(node_type_t::UNICODE_RANGE, node_type_t::NULL_TOKEN):
        if(op == node_type_t::MULTIPLY)
        {
            return rhs;
        }
        error::instance() << f_current->get_position()
                << "unicode_range * unicode_range is the only multiplicative operator accepted with unicode ranges, '/' and '%' are not allowed."
                << error_mode_t::ERROR_ERROR;
        return node::pointer_t();

    case mix_node_types(node_type_t::UNICODE_RANGE, node_type_t::UNICODE_RANGE):
        if(op == node_type_t::MULTIPLY)
        {
            unicode_range_t const lrange(static_cast<range_value_t>(lhs->get_integer()));
            unicode_range_t const rrange(static_cast<range_value_t>(rhs->get_integer()));
            range_value_t const start(std::max(lrange.get_start(), rrange.get_start()));
            range_value_t const end  (std::min(lrange.get_end(),   rrange.get_end()));
            node::pointer_t result;
            if(start <= end)
            {
                result.reset(new node(node_type_t::UNICODE_RANGE, lhs->get_position()));
                unicode_range_t range(start, end);
                result->set_integer(range.get_range());
            }
            else
            {
                // range becomes null (not characters are in common)
                result.reset(new node(node_type_t::NULL_TOKEN, lhs->get_position()));
            }
            return result;
        }
        error::instance() << f_current->get_position()
                << "unicode_range * unicode_range is the only multiplicative operator accepted with unicode ranges, '/' and '%' are not allowed."
                << error_mode_t::ERROR_ERROR;
        return node::pointer_t();

    case mix_node_types(node_type_t::INTEGER, node_type_t::COLOR):
        if(op != node_type_t::MULTIPLY)
        {
            error::instance() << f_current->get_position()
                    << "'number / color' and 'number % color' are not available."
                    << error_mode_t::ERROR_ERROR;
            return node::pointer_t();
        }
        swap(lhs, rhs);
#if __cplusplus >= 201700
        [[fallthrough]];
#endif
    case mix_node_types(node_type_t::COLOR, node_type_t::INTEGER):
        bf = static_cast<decimal_number_t>(rhs->get_integer());
        goto color_multiplicative;

    case mix_node_types(node_type_t::DECIMAL_NUMBER, node_type_t::COLOR):
    case mix_node_types(node_type_t::PERCENT, node_type_t::COLOR):
        if(op != node_type_t::MULTIPLY)
        {
            error::instance() << f_current->get_position()
                    << "'number / color' and 'number % color' are not available."
                    << error_mode_t::ERROR_ERROR;
            return node::pointer_t();
        }
        swap(lhs, rhs);
#if __cplusplus >= 201700
        [[fallthrough]];
#endif
    case mix_node_types(node_type_t::COLOR, node_type_t::DECIMAL_NUMBER):
    case mix_node_types(node_type_t::COLOR, node_type_t::PERCENT):
        bf = rhs->get_decimal_number();
color_multiplicative:
        if(rhs->is(node_type_t::PERCENT)
        || rhs->get_string() == "")
        {
            color c(lhs->get_color());
            color_component_t red;
            color_component_t green;
            color_component_t blue;
            color_component_t alpha;
            c.get_color(red, green, blue, alpha);
            switch(op)
            {
            case node_type_t::MULTIPLY:
                red   *= bf;
                green *= bf;
                blue  *= bf;
                alpha *= bf;
                break;

            case node_type_t::DIVIDE:
                red   /= bf;
                green /= bf;
                blue  /= bf;
                alpha /= bf;
                break;

            case node_type_t::MODULO:
                red   = fmod(red,   bf);
                green = fmod(green, bf);
                blue  = fmod(blue,  bf);
                alpha = fmod(alpha, bf);
                break;

            default:
                throw csspp_exception_logic("expression.cpp:multiply(): unexpected operator."); // LCOV_EXCL_LINE

            }
            c.set_color(red, green, blue, alpha);
            node::pointer_t result(new node(node_type_t::COLOR, lhs->get_position()));
            result->set_color(c);
            return result;
        }
        error::instance() << f_current->get_position()
                << "color factors must be unit less values, "
                << bf
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
            switch(op)
            {
            case node_type_t::MULTIPLY:
                lred   *= rred;
                lgreen *= rgreen;
                lblue  *= rblue;
                lalpha *= ralpha;
                break;

            case node_type_t::DIVIDE:
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
                if(rred == 0.0
                || rgreen == 0.0
                || rblue == 0.0
                || ralpha == 0.0)
#pragma GCC diagnostic pop
                {
                    error::instance() << f_current->get_position()
                            << "color division does not accept any color component set to zero."
                            << error_mode_t::ERROR_ERROR;
                    return node::pointer_t();
                }
                lred   /= rred;
                lgreen /= rgreen;
                lblue  /= rblue;
                lalpha /= ralpha;
                break;

            case node_type_t::MODULO:
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
                if(rred == 0.0
                || rgreen == 0.0
                || rblue == 0.0
                || ralpha == 0.0)
#pragma GCC diagnostic pop
                {
                    error::instance() << f_current->get_position()
                            << "color modulo does not accept any color component set to zero."
                            << error_mode_t::ERROR_ERROR;
                    return node::pointer_t();
                }
                lred   = fmod(lred   , rred  );
                lgreen = fmod(lgreen , rgreen);
                lblue  = fmod(lblue  , rblue );
                lalpha = fmod(lalpha , ralpha);
                break;

            default:
                throw csspp_exception_logic("expression.cpp:multiply(): unexpected operator."); // LCOV_EXCL_LINE

            }
            lc.set_color(lred, lgreen, lblue, lalpha);
            node::pointer_t result(new node(node_type_t::COLOR, lhs->get_position()));
            result->set_color(lc);
            return result;
        }

    default:
        error::instance() << f_current->get_position()
                << "incompatible types between "
                << lhs->get_type()
                << " and "
                << rhs->get_type()
                << " for operator '*', '/', or '%'."
                << error_mode_t::ERROR_ERROR;
        return node::pointer_t();

    }

    if(op == node_type_t::DIVIDE && f_divide_font_metrics)
    {
        // this is that special case of a FONT_METRICS node
        node::pointer_t result(new node(node_type_t::FONT_METRICS, lhs->get_position()));

        // convert integers
        // (we don't need to convert to do the set, but I find it cleaner this way)
        if(type == node_type_t::INTEGER)
        {
            af = static_cast<decimal_number_t>(ai);
            bf = static_cast<decimal_number_t>(bi);
        }

        result->set_font_size(af);
        result->set_line_height(bf);
        if(lhs->is(node_type_t::PERCENT))
        {
            result->set_dim1("%");
        }
        else
        {
            result->set_dim1(lhs->get_string());
        }
        if(rhs->is(node_type_t::PERCENT))
        {
            result->set_dim2("%");
        }
        else
        {
            result->set_dim2(rhs->get_string());
        }
        return result;
    }

    node::pointer_t result(new node(type, lhs->get_position()));

    if(type != node_type_t::PERCENT)
    {
        // dimensions do not need to be equal
        //
        // a * b  results in a dimension such as 'px * em'
        // a / b  results in a dimension such as 'px / em'
        //
        // multiple products/divisions can occur in which case the '/' becomes
        // the separator as in:
        //
        // a * b / c / d  results in a dimension such as  'px * em / cm * vw'
        //
        // when the same dimension appears on the left and right of a /
        // then it can be removed, allowing conversions from one type to
        // another, so:
        //
        // 'px * em / px' == 'em'
        //
        // modulo, like additive operators, requires both dimensions to be
        // exactly equal or both numbers to not have a dimension
        //
        std::string ldim(lhs->is(node_type_t::PERCENT) ? "" : lhs->get_string());
        std::string rdim(rhs->is(node_type_t::PERCENT) ? "" : rhs->get_string());
        switch(op)
        {
        case node_type_t::MODULO:
            // modulo requires both dimensions to be equal
            if(ldim != rdim)
            {
                error::instance() << lhs->get_position()
                        << "incompatible dimensions (\""
                        << ldim
                        << "\" and \""
                        << rdim
                        << "\") cannot be used with operator '%'."
                        << error_mode_t::ERROR_ERROR;
                return node::pointer_t();
            }
            // set ldim or rdim, they equal each other anyway
            result->set_string(ldim);
            break;

        case node_type_t::MULTIPLY:
        case node_type_t::DIVIDE:
            result->set_string(multiplicative_dimension(lhs->get_position(), ldim, op, rdim));
            break;

        default:
            // that should never happen
            throw csspp_exception_logic("expression.cpp:multiply(): unexpected operator."); // LCOV_EXCL_LINE

        }
    }

    switch(type)
    {
    case node_type_t::INTEGER:
        switch(op)
        {
        case node_type_t::MULTIPLY:
            result->set_integer(ai * bi);
            break;

        case node_type_t::DIVIDE:
            if(bi == 0)
            {
                error::instance() << lhs->get_position()
                        << "division by zero."
                        << error_mode_t::ERROR_ERROR;
                return node::pointer_t();
            }
            result->set_integer(ai / bi);
            break;

        case node_type_t::MODULO:
            if(bi == 0)
            {
                error::instance() << lhs->get_position()
                        << "modulo by zero."
                        << error_mode_t::ERROR_ERROR;
                return node::pointer_t();
            }
            result->set_integer(ai % bi);
            break;

        default:
            throw csspp_exception_logic("expression.cpp:multiply(): unexpected operator."); // LCOV_EXCL_LINE

        }
        break;

    case node_type_t::DECIMAL_NUMBER:
    case node_type_t::PERCENT:
        switch(op)
        {
        case node_type_t::MULTIPLY:
            result->set_decimal_number(af * bf);
            break;

        case node_type_t::DIVIDE:
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
            if(bf == 0.0)
#pragma GCC diagnostic pop
            {
                error::instance() << lhs->get_position()
                        << "division by zero."
                        << error_mode_t::ERROR_ERROR;
                return node::pointer_t();
            }
            result->set_decimal_number(af / bf);
            break;

        case node_type_t::MODULO:
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
            if(bf == 0.0)
#pragma GCC diagnostic pop
            {
                error::instance() << lhs->get_position()
                        << "modulo by zero."
                        << error_mode_t::ERROR_ERROR;
                return node::pointer_t();
            }
            result->set_decimal_number(fmod(af, bf));
            break;

        default:
            throw csspp_exception_logic("expression.cpp:multiply(): unexpected operator."); // LCOV_EXCL_LINE

        }
        break;

    default:
        throw csspp_exception_logic("expression.cpp:multiply(): 'type' set to a value which is not handled here."); // LCOV_EXCL_LINE

    }

    return result;
}

node::pointer_t expression::multiplicative()
{
    // multiplicative: power
    //               | multiplicative '*' power
    //               | multiplicative '/' power
    //               | multiplicative '%' power

    node::pointer_t result(power());
    if(!result)
    {
        return node::pointer_t();
    }

    node_type_t op(multiplicative_operator(f_current));
    while(op != node_type_t::UNKNOWN)
    {
        // skip the multiplicative operator
        next();

        node::pointer_t rhs(power());
        if(rhs == nullptr)
        {
            return node::pointer_t();
        }

        // apply the multiplicative operation
        result = multiply(op, result, rhs);
        if(!result)
        {
            return node::pointer_t();
        }

        op = multiplicative_operator(f_current);
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
