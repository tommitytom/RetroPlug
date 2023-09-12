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

#include    <algorithm>
#include    <cmath>
#include    <climits>
#include    <iostream>

namespace csspp
{

namespace
{

int g_unique_id_counter = 0;

decimal_number_t dimension_to_radians(position const & pos, decimal_number_t n, std::string const & dimension)
{
    if(dimension == "rad")
    {
        // all good as is
        return n;
    }
    else if(dimension == "deg"
         || dimension == "") // no angle dimension, use as if it were degrees (which has been the default in CSS)
    {
        // convert degrees to radians
        return n * M_PI / 180.0;
    }
    else if(dimension == "grad")
    {
        // convert grads to radians
        return n * M_PI / 200.0;
    }
    else if(dimension == "turn")
    {
        // convert turns to radians
        return n * M_PI * 2.0;
    }
    else
    {
        error::instance() << pos
                << "trigonometry functions expect an angle (deg, grad, rad, turn) as a parameter."
                << error_mode_t::ERROR_ERROR;

        // keep 'n' as is... what else could we do?
        return n;
    }
}

} // no name namespace

void expression::set_unique_id_counter(int counter)
{
    g_unique_id_counter = counter;
}

int expression::get_unique_id_counter()
{
    return g_unique_id_counter;
}

node::pointer_t expression::internal_function__get_any(node::pointer_t func, size_t argn)
{
    if(argn >= func->size())
    {
        return node::pointer_t();  // LCOV_EXCL_LINE
    }

    node::pointer_t arg(func->get_child(argn));
    if(arg->size() != 1)
    {
        return node::pointer_t();
    }

    return arg->get_child(0);
}

node::pointer_t expression::internal_function__get_color(node::pointer_t func, size_t argn, color & col)
{
    if(argn >= func->size())
    {
        return node::pointer_t();  // LCOV_EXCL_LINE
    }

    node::pointer_t arg(func->get_child(argn));
    if(arg->size() != 1)
    {
        return node::pointer_t();
    }

    node::pointer_t value(arg->get_child(0));
    if(value->is(node_type_t::COLOR))
    {
        col = value->get_color();
        return value;
    }

    return node::pointer_t();
}

node::pointer_t expression::internal_function__get_number(node::pointer_t func, size_t argn, decimal_number_t & number)
{
    if(argn >= func->size())
    {
        return node::pointer_t();
    }

    node::pointer_t arg(func->get_child(argn));
    if(arg->size() != 1)
    {
        return node::pointer_t();
    }

    node::pointer_t value(arg->get_child(0));
    if(value->is(node_type_t::INTEGER))
    {
        number = static_cast<decimal_number_t>(value->get_integer());
        return value;
    }

    if(value->is(node_type_t::DECIMAL_NUMBER))
    {
        number = value->get_decimal_number();
        return value;
    }

    return node::pointer_t();
}

node::pointer_t expression::internal_function__get_number_or_percent(node::pointer_t func, size_t argn, decimal_number_t & number)
{
    if(argn >= func->size())
    {
        return node::pointer_t();  // LCOV_EXCL_LINE
    }

    node::pointer_t arg(func->get_child(argn));
    if(arg->size() != 1)
    {
        return node::pointer_t();
    }

    node::pointer_t value(arg->get_child(0));
    if(value->is(node_type_t::INTEGER))
    {
        number = static_cast<decimal_number_t>(value->get_integer());
        return value;
    }

    if(value->is(node_type_t::DECIMAL_NUMBER))
    {
        number = value->get_decimal_number();
        return value;
    }

    if(value->is(node_type_t::PERCENT))
    {
        number = value->get_decimal_number();
        return value;
    }

    return node::pointer_t();
}

node::pointer_t expression::internal_function__get_string(node::pointer_t func, size_t argn, std::string & str)
{
    if(argn >= func->size())
    {
        return node::pointer_t();  // LCOV_EXCL_LINE
    }

    node::pointer_t arg(func->get_child(argn));
    if(arg->size() != 1)
    {
        return node::pointer_t();
    }

    node::pointer_t value(arg->get_child(0));
    if(value->is(node_type_t::STRING))
    {
        str = value->get_string();
        return value;
    }

    return node::pointer_t();
}

node::pointer_t expression::internal_function__get_string_or_identifier(node::pointer_t func, size_t argn, std::string & str)
{
    if(argn >= func->size())
    {
        return node::pointer_t();   // LCOV_EXCL_LINE
    }

    node::pointer_t arg(func->get_child(argn));
    if(arg->size() != 1)
    {
        return node::pointer_t();
    }

    node::pointer_t value(arg->get_child(0));
    if(value->is(node_type_t::IDENTIFIER)
    || value->is(node_type_t::STRING))
    {
        str = value->get_string();
        return value;
    }

    return node::pointer_t();
}

node::pointer_t expression::internal_function__abs(node::pointer_t func)
{
    // abs(number)
    decimal_number_t n;
    node::pointer_t number(internal_function__get_number(func, 0, n));
    if(number)
    {
        if(number->is(node_type_t::INTEGER))
        {
            number->set_integer(labs(number->get_integer()));
        }
        else
        {
            number->set_decimal_number(fabs(number->get_decimal_number()));
        }
        return number;
    }

    error::instance() << f_current->get_position()
            << "abs() expects a number as parameter."
            << error_mode_t::ERROR_ERROR;

    return node::pointer_t();
}

node::pointer_t expression::internal_function__acos(node::pointer_t func)
{
    // acos(number)
    decimal_number_t n;
    node::pointer_t number(internal_function__get_number(func, 0, n));
    if(number)
    {
        if(number->is(node_type_t::INTEGER))
        {
            number.reset(new node(node_type_t::DECIMAL_NUMBER, number->get_position()));
        }
        // should we return the angle in degrees instead?
        number->set_decimal_number(acos(n));
        number->set_string("rad");
        return number;
    }

    error::instance() << f_current->get_position()
            << "acos() expects a number as parameter."
            << error_mode_t::ERROR_ERROR;

    return node::pointer_t();
}

node::pointer_t expression::internal_function__alpha(node::pointer_t func)
{
    // alpha(color)
    color c;
    node::pointer_t col(internal_function__get_color(func, 0, c));
    if(col)
    {
        color_component_t r;
        color_component_t g;
        color_component_t b;
        color_component_t a;
        c.get_color(r, g, b, a);
        node::pointer_t component(new node(node_type_t::DECIMAL_NUMBER, func->get_position()));
        component->set_decimal_number(a);
        return component;
    }

    error::instance() << f_current->get_position()
            << "alpha() expects a color as parameter."
            << error_mode_t::ERROR_ERROR;

    return node::pointer_t();
}

node::pointer_t expression::internal_function__asin(node::pointer_t func)
{
    // asin(number)
    decimal_number_t n;
    node::pointer_t number(internal_function__get_number(func, 0, n));
    if(number)
    {
        if(number->is(node_type_t::INTEGER))
        {
            number.reset(new node(node_type_t::DECIMAL_NUMBER, number->get_position()));
        }
        // should we return the angle in degrees instead?
        number->set_decimal_number(asin(n));
        number->set_string("rad");
        return number;
    }

    error::instance() << f_current->get_position()
            << "asin() expects a number as parameter."
            << error_mode_t::ERROR_ERROR;

    return node::pointer_t();
}

node::pointer_t expression::internal_function__atan(node::pointer_t func)
{
    // atan(number)
    decimal_number_t n;
    node::pointer_t number(internal_function__get_number(func, 0, n));
    if(number)
    {
        if(number->is(node_type_t::INTEGER))
        {
            number.reset(new node(node_type_t::DECIMAL_NUMBER, number->get_position()));
        }
        // should we return the angle in degrees instead?
        number->set_decimal_number(atan(n));
        number->set_string("rad");
        return number;
    }

    error::instance() << f_current->get_position()
            << "atan() expects a number as parameter."
            << error_mode_t::ERROR_ERROR;

    return node::pointer_t();
}

node::pointer_t expression::internal_function__blue(node::pointer_t func)
{
    // blue(color)
    color c;
    node::pointer_t col(internal_function__get_color(func, 0, c));
    if(col)
    {
        color_component_t r;
        color_component_t g;
        color_component_t b;
        color_component_t a;
        c.get_color(r, g, b, a);
        node::pointer_t component(new node(node_type_t::INTEGER, func->get_position()));
        component->set_integer(static_cast<integer_t>(b * 255.0 + 0.5));
        return component;
    }

    error::instance() << f_current->get_position()
            << "blue() expects a color as parameter."
            << error_mode_t::ERROR_ERROR;

    return node::pointer_t();
}

node::pointer_t expression::internal_function__ceil(node::pointer_t func)
{
    // ceil(number)
    decimal_number_t n;
    node::pointer_t number(internal_function__get_number(func, 0, n));
    if(number)
    {
        if(number->is(node_type_t::DECIMAL_NUMBER))
        {
            number->set_decimal_number(ceil(number->get_decimal_number()));
        }
        return number;
    }

    error::instance() << f_current->get_position()
            << "ceil() expects a number as parameter."
            << error_mode_t::ERROR_ERROR;

    return node::pointer_t();
}

node::pointer_t expression::internal_function__cos(node::pointer_t func)
{
    // cos(number)
    decimal_number_t n;
    node::pointer_t number(internal_function__get_number(func, 0, n));
    if(number)
    {
        std::string const dimension(number->get_string());
        if(number->is(node_type_t::INTEGER))
        {
            number.reset(new node(node_type_t::DECIMAL_NUMBER, number->get_position()));
        }
        else
        {
            // we "lose" the dimension
            number->set_string("");
        }
        n = dimension_to_radians(func->get_position(), n, dimension);
        number->set_decimal_number(cos(n));
        return number;
    }

    error::instance() << f_current->get_position()
            << "cos() expects an angle as parameter."
            << error_mode_t::ERROR_ERROR;

    return node::pointer_t();
}

node::pointer_t expression::internal_function__decimal_number(node::pointer_t func)
{
    // decimal_number(expr)
    node::pointer_t any(internal_function__get_any(func, 0));
    if(any)
    {
        switch(any->get_type())
        {
        case node_type_t::DECIMAL_NUMBER:
            // already a decimal number, return as is
            return any;

        case node_type_t::PERCENT:
            {
                node::pointer_t number(new node(node_type_t::DECIMAL_NUMBER, any->get_position()));
                number->set_decimal_number(any->get_decimal_number());
                return number;
            }

        case node_type_t::INTEGER:
            {
                node::pointer_t number(new node(node_type_t::DECIMAL_NUMBER, any->get_position()));
                number->set_decimal_number(any->get_integer());
                number->set_string(any->get_string());
                return number;
            }

        case node_type_t::IDENTIFIER:
        case node_type_t::STRING:
        case node_type_t::URL:
            {
                std::stringstream ss;
                ss << any->get_string();
                lexer l(ss, any->get_position());
                node::pointer_t number(l.next_token());
                if(number->is(node_type_t::WHITESPACE))
                {
                    number = l.next_token();
                }
                switch(number->get_type())
                {
                case node_type_t::DECIMAL_NUMBER:
                    return number;

                case node_type_t::PERCENT:
                    {
                        node::pointer_t result(new node(node_type_t::DECIMAL_NUMBER, any->get_position()));
                        result->set_decimal_number(number->get_decimal_number());
                        return result;
                    }

                case node_type_t::INTEGER:
                    {
                        node::pointer_t result(new node(node_type_t::DECIMAL_NUMBER, any->get_position()));
                        result->set_decimal_number(number->get_integer());
                        result->set_string(number->get_string());
                        return result;
                    }

                default:
                    break;

                }
            }
            error::instance() << f_current->get_position()
                    << "decimal_number() expects a string parameter to represent a valid integer, decimal number, or percent value."
                    << error_mode_t::ERROR_ERROR;
            return node::pointer_t();

        default:
            break;

        }
    }

    error::instance() << f_current->get_position()
            << "decimal_number() expects one value as parameter."
            << error_mode_t::ERROR_ERROR;

    return node::pointer_t();
}

node::pointer_t expression::internal_function__floor(node::pointer_t func)
{
    // floor(number)
    decimal_number_t n;
    node::pointer_t number(internal_function__get_number(func, 0, n));
    if(number)
    {
        if(number->is(node_type_t::DECIMAL_NUMBER))
        {
            number->set_decimal_number(floor(number->get_decimal_number()));
        }
        return number;
    }

    error::instance() << f_current->get_position()
            << "floor() expects a number as parameter."
            << error_mode_t::ERROR_ERROR;

    return node::pointer_t();
}

node::pointer_t expression::internal_function__frgb(node::pointer_t func)
{
    // frgb(color)
    // frgb(fred, fgreen, fblue)
    color c;
    node::pointer_t col(internal_function__get_color(func, 0, c));
    if(col)
    {
        // force alpha to 1.0
        color_component_t r;
        color_component_t g;
        color_component_t b;
        color_component_t a;
        c.get_color(r, g, b, a);
        c.set_color(r, g, b, static_cast<color_component_t>(1.0));
        col->set_color(c);
        return col;
    }
    else
    {
        decimal_number_t r;
        decimal_number_t g;
        decimal_number_t b;

        node::pointer_t col1(internal_function__get_number(func, 0, r));
        node::pointer_t col2(internal_function__get_number(func, 1, g));
        node::pointer_t col3(internal_function__get_number(func, 2, b));

        if(col1 && col2 && col3)
        {
            // force alpha to 1.0
            c.set_color(r, g, b, 1.0);
            col.reset(new node(node_type_t::COLOR, func->get_position()));
            col->set_color(c);
            return col;
        }
    }

    error::instance() << f_current->get_position()
            << "frgb() expects exactly one color parameter or three numbers (Red, Green, Blue)."
            << error_mode_t::ERROR_ERROR;

    return node::pointer_t();
}

node::pointer_t expression::internal_function__frgba(node::pointer_t func)
{
    // frgba(color, alpha)
    // frgba(fred, fgreen, fblue, alpha)
    color c;
    decimal_number_t a;
    node::pointer_t col(internal_function__get_color(func, 0, c));
    node::pointer_t alpha(internal_function__get_number(func, 1, a));
    if(col && alpha)
    {
        // replace alpha
        color_component_t r;
        color_component_t g;
        color_component_t b;
        color_component_t old_a;
        c.get_color(r, g, b, old_a);
        c.set_color(r, g, b, static_cast<color_component_t>(a));
        col->set_color(c);
        return col;
    }
    else
    {
        decimal_number_t r;
        decimal_number_t g;
        decimal_number_t b;

        node::pointer_t col1(internal_function__get_number(func, 0, r));
        node::pointer_t col2(internal_function__get_number(func, 1, g));
        node::pointer_t col3(internal_function__get_number(func, 2, b));
        node::pointer_t col4(internal_function__get_number(func, 3, a));

        if(col1 && col2 && col3 && col4)
        {
            // set color with alpha
            c.set_color(r, g, b, a);
            col.reset(new node(node_type_t::COLOR, func->get_position()));
            col->set_color(c);
            return col;
        }
    }

    error::instance() << f_current->get_position()
            << "frgba() expects exactly one color parameter followed by one number (Color, Alpha), or four numbers (Red, Green, Blue, Alpha)."
            << error_mode_t::ERROR_ERROR;

    return node::pointer_t();
}

node::pointer_t expression::internal_function__function_exists(node::pointer_t func)
{
    // function_exists(name)
    std::string name;
    node::pointer_t id(internal_function__get_string_or_identifier(func, 0, name));
    if(id && !name.empty())
    {
        node::pointer_t result(new node(node_type_t::BOOLEAN, func->get_position()));

        // although variables were already applied they will still be
        // defined when we reach these lines of code
        if(f_variable_handler)
        {
            node::pointer_t var(f_variable_handler->get_variable(name, true));
            if(var
            && var->is(node_type_t::LIST)
            && !var->empty()
            && (var->get_child(0)->is(node_type_t::VARIABLE_FUNCTION) // $<name>()
            || var->get_child(0)->is(node_type_t::FUNCTION)))         // @mixin <name>()
            {
                result->set_boolean(true);
            }
        }
        // else -- default is already false

        return result;
    }

    error::instance() << f_current->get_position()
            << "function_exists() expects a string or an identifier as parameter."
            << error_mode_t::ERROR_ERROR;

    return node::pointer_t();
}

node::pointer_t expression::internal_function__green(node::pointer_t func)
{
    // green(color)
    color c;
    node::pointer_t col(internal_function__get_color(func, 0, c));
    if(col)
    {
        color_component_t r;
        color_component_t g;
        color_component_t b;
        color_component_t a;
        c.get_color(r, g, b, a);
        node::pointer_t component(new node(node_type_t::INTEGER, func->get_position()));
        component->set_integer(static_cast<integer_t>(g * 255.0 + 0.5));
        return component;
    }

    error::instance() << f_current->get_position()
            << "green() expects a color as parameter."
            << error_mode_t::ERROR_ERROR;

    return node::pointer_t();
}

node::pointer_t expression::internal_function__global_variable_exists(node::pointer_t func)
{
    // global_variable_exists(name)
    std::string name;
    node::pointer_t id(internal_function__get_string_or_identifier(func, 0, name));
    if(id && !name.empty())
    {
        node::pointer_t result(new node(node_type_t::BOOLEAN, func->get_position()));

        // although variables were already applied they will still be
        // defined when we reach these lines of code
        if(f_variable_handler)
        {
            node::pointer_t var(f_variable_handler->get_variable(name, true));
            if(var
            && var->is(node_type_t::LIST)
            && !var->empty()
            && (var->get_child(0)->is(node_type_t::VARIABLE)      // $<name>
            || var->get_child(0)->is(node_type_t::IDENTIFIER)))   // @mixin <name>
            {
                result->set_boolean(true);
            }
        }
        // else -- default is already false

        return result;
    }

    error::instance() << f_current->get_position()
            << "global_variable_exists() expects a string or an identifier as parameter."
            << error_mode_t::ERROR_ERROR;

    return node::pointer_t();
}

node::pointer_t expression::internal_function__hsl(node::pointer_t func)
{
    // hsl(red, green, blue)
    decimal_number_t h;
    decimal_number_t s;
    decimal_number_t l;

    node::pointer_t col1(internal_function__get_number(func, 0, h));
    node::pointer_t col2(internal_function__get_number_or_percent(func, 1, s));
    node::pointer_t col3(internal_function__get_number_or_percent(func, 2, l));

    if(col1 && col2 && col3)
    {
        // transform the angle from whatever dimension it is defined as to
        // radians as expected by set_hsl()
        h = dimension_to_radians(func->get_position(), h, col1->get_string());

        // force alpha to 1.0
        color c;
        c.set_hsl(h, s, l, 1.0);
        node::pointer_t col(new node(node_type_t::COLOR, func->get_position()));
        col->set_color(c);
        return col;
    }

    error::instance() << f_current->get_position()
            << "hsl() expects exactly three numbers: Hue (angle), Saturation (%), and Lightness (%)."
            << error_mode_t::ERROR_ERROR;

    return node::pointer_t();
}

node::pointer_t expression::internal_function__hsla(node::pointer_t func)
{
    // hsla(red, green, blue, alpha)
    decimal_number_t h;
    decimal_number_t s;
    decimal_number_t l;
    decimal_number_t a;

    node::pointer_t col1(internal_function__get_number(func, 0, h));
    node::pointer_t col2(internal_function__get_number_or_percent(func, 1, s));
    node::pointer_t col3(internal_function__get_number_or_percent(func, 2, l));
    node::pointer_t col4(internal_function__get_number(func, 3, a));

    if(col1 && col2 && col3 && col4)
    {
        // transform the angle from whatever dimension it is defined as to
        // radians as expected by set_hsl()
        h = dimension_to_radians(func->get_position(), h, col1->get_string());

        // set color with alpha
        color c;
        c.set_hsl(h, s, l, a);
        node::pointer_t col(new node(node_type_t::COLOR, func->get_position()));
        col->set_color(c);
        return col;
    }

    error::instance() << f_current->get_position()
            << "hsla() expects exactly four numbers: Hue (angle), Saturation (%), Lightness (%), and Alpha (0.0 to 1.0)."
            << error_mode_t::ERROR_ERROR;

    return node::pointer_t();
}

node::pointer_t expression::internal_function__hue(node::pointer_t func)
{
    // hue(color)
    color c;
    node::pointer_t col(internal_function__get_color(func, 0, c));
    if(col)
    {
        color_component_t hue;
        color_component_t saturation;
        color_component_t lightness;
        color_component_t a;
        c.get_hsl(hue, saturation, lightness, a);
        node::pointer_t component(new node(node_type_t::DECIMAL_NUMBER, func->get_position()));
        component->set_decimal_number(hue * 180.0 / M_PI);
        component->set_string("deg");
        return component;
    }

    error::instance() << f_current->get_position()
            << "hue() expects a color as parameter."
            << error_mode_t::ERROR_ERROR;

    return node::pointer_t();
}

node::pointer_t expression::internal_function__identifier(node::pointer_t func)
{
    // identifier(expr)
    node::pointer_t any(internal_function__get_any(func, 0));
    if(any)
    {
        switch(any->get_type())
        {
        case node_type_t::IDENTIFIER:
            // already an identifier, return as is
            return any;

        case node_type_t::COLOR:
        case node_type_t::DECIMAL_NUMBER:
        case node_type_t::INTEGER:
        case node_type_t::PERCENT:
            {
                node::pointer_t id(new node(node_type_t::IDENTIFIER, any->get_position()));
                id->set_string(any->to_string(0));
                return id;
            }

        case node_type_t::STRING:
        case node_type_t::URL:
            {
                node::pointer_t id(new node(node_type_t::IDENTIFIER, any->get_position()));
                id->set_string(any->get_string());
                return id;
            }

        default:
            break;

        }
    }

    error::instance() << f_current->get_position()
            << "identifier() expects one value as parameter."
            << error_mode_t::ERROR_ERROR;

    return node::pointer_t();
}

node::pointer_t expression::internal_function__if(node::pointer_t func)
{
    // if(condition, if-true, if-false)
    node::pointer_t arg1(func->get_child(0));
    if(arg1->size() != 1)
    {
        error::instance() << f_current->get_position()
                << "if() expects a boolean as its first argument."
                << error_mode_t::ERROR_ERROR;
        return node::pointer_t();
    }
    else
    {
        // if boolean() returns true when arg1 is considered true
        //
        // Note:
        // It generates an error and returns false if the node passed in
        // is not considered to be a valid boolean node.
        //
        bool const r(boolean(arg1->get_child(0)));
        node::pointer_t result(func->get_child(r ? 1 : 2));
        if(result->size() == 1)
        {
            // very simple result, return as is
            return result->get_child(0);
        }
        // complex result (multiple nodes), return in a list
        node::pointer_t list(new node(node_type_t::LIST, result->get_position()));
        list->take_over_children_of(result); // TBD: should we use clone() instead?
        return list;
    }
}

node::pointer_t expression::internal_function__inspect(node::pointer_t func)
{
    // inspect(expression)
    //
    // no need to check whether child 0 exists since we get called only
    // if the function has exactly 1 argument
    //
    node::pointer_t any(func->get_child(0));
    node::pointer_t result(new node(node_type_t::STRING, any->get_position()));
    result->set_string(any->to_string(node::g_to_string_flag_show_quotes));
    return result;
}

node::pointer_t expression::internal_function__integer(node::pointer_t func)
{
    // integer(expression)
    node::pointer_t any(internal_function__get_any(func, 0));
    if(any)
    {
        switch(any->get_type())
        {
        case node_type_t::INTEGER:
            // already an integer, return as is
            return any;

        case node_type_t::DECIMAL_NUMBER:
            {
                node::pointer_t number(new node(node_type_t::INTEGER, any->get_position()));
                number->set_integer(any->get_decimal_number());
                number->set_string(any->get_string());
                return number;
            }

        case node_type_t::PERCENT:
            {
                node::pointer_t number(new node(node_type_t::INTEGER, any->get_position()));
                number->set_integer(any->get_decimal_number());
                return number;
            }

        case node_type_t::IDENTIFIER:
        case node_type_t::STRING:
        case node_type_t::URL:
            {
                std::stringstream ss;
                ss << any->get_string();
                lexer l(ss, any->get_position());
                node::pointer_t number(l.next_token());
                if(number->is(node_type_t::WHITESPACE))
                {
                    number = l.next_token();
                }
                switch(number->get_type())
                {
                case node_type_t::INTEGER:
                    return number;

                case node_type_t::DECIMAL_NUMBER:
                    {
                        node::pointer_t result(new node(node_type_t::INTEGER, any->get_position()));
                        result->set_integer(number->get_decimal_number());
                        result->set_string(number->get_string());
                        return result;
                    }

                case node_type_t::PERCENT:
                    {
                        node::pointer_t result(new node(node_type_t::INTEGER, any->get_position()));
                        result->set_integer(number->get_decimal_number());
                        return result;
                    }

                default:
                    break;

                }
            }
            error::instance() << f_current->get_position()
                    << "decimal_number() expects a string parameter to represent a valid integer, decimal number, or percent value."
                    << error_mode_t::ERROR_ERROR;
            return node::pointer_t();

        default:
            break;

        }
    }

    error::instance() << f_current->get_position()
            << "integer() expects one value as parameter."
            << error_mode_t::ERROR_ERROR;

    return node::pointer_t();
}

node::pointer_t expression::internal_function__lightness(node::pointer_t func)
{
    // lightness(color)
    color c;
    node::pointer_t col(internal_function__get_color(func, 0, c));
    if(col)
    {
        color_component_t hue;
        color_component_t saturation;
        color_component_t lightness;
        color_component_t a;
        c.get_hsl(hue, saturation, lightness, a);
        node::pointer_t component(new node(node_type_t::PERCENT, func->get_position()));
        component->set_decimal_number(lightness);
        return component;
    }

    error::instance() << f_current->get_position()
            << "lightness() expects a color as parameter."
            << error_mode_t::ERROR_ERROR;

    return node::pointer_t();
}

node::pointer_t expression::internal_function__log(node::pointer_t func)
{
    // log(number)
    decimal_number_t n;
    node::pointer_t number(internal_function__get_number(func, 0, n));
    if(number)
    {
        if(!number->get_string().empty())
        {
            error::instance() << f_current->get_position()
                    << "log() expects a unit less number as parameter."
                    << error_mode_t::ERROR_ERROR;

            return node::pointer_t();
        }
        if(n <= 0.0)
        {
            error::instance() << f_current->get_position()
                    << "log() expects a positive number as parameter."
                    << error_mode_t::ERROR_ERROR;

            return node::pointer_t();
        }
        if(number->is(node_type_t::INTEGER))
        {
            number.reset(new node(node_type_t::DECIMAL_NUMBER, number->get_position()));
        }
        number->set_decimal_number(log(n));
        return number;
    }

    error::instance() << f_current->get_position()
            << "log() expects a number as parameter."
            << error_mode_t::ERROR_ERROR;

    return node::pointer_t();
}

node::pointer_t expression::internal_function__max(node::pointer_t func)
{
    // max(n1, n2, ...)
    node::pointer_t number;
    decimal_number_t maximum(0.0);
    std::string dimension;
    size_t const max_children(func->size());
    for(size_t idx(0); idx < max_children; ++idx)
    {
        decimal_number_t r;
        node::pointer_t n(internal_function__get_number_or_percent(func, idx, r));
        if(n)
        {
            if(idx == 0)
            {
                if(n->is(node_type_t::PERCENT))
                {
                    dimension = "%";
                }
                else
                {
                    dimension = n->get_string();
                }
            }
            else
            {
                bool valid_dimension(true);
                if(n->is(node_type_t::PERCENT))
                {
                    valid_dimension = dimension == "%";
                }
                else
                {
                    valid_dimension = dimension == n->get_string();
                }
                if(!valid_dimension)
                {
                    // all dimensions must be the same
                    error::instance() << f_current->get_position()
                            << "max() expects all numbers to have the same dimension."
                            << error_mode_t::ERROR_ERROR;
                }
            }
            if(idx == 0 || r > maximum)
            {
                number = n;
                maximum = r;
            }
        }
        else
        {
            error::instance() << f_current->get_position()
                    << "max() expects any number of numbers."
                    << error_mode_t::ERROR_ERROR;
        }
    }
    return number;
}

node::pointer_t expression::internal_function__min(node::pointer_t func)
{
    // min(n1, n2, ...)
    node::pointer_t number;
    decimal_number_t minimum(0.0);
    std::string dimension;
    size_t const max_children(func->size());
    for(size_t idx(0); idx < max_children; ++idx)
    {
        decimal_number_t r;
        node::pointer_t n(internal_function__get_number_or_percent(func, idx, r));
        if(n)
        {
            if(idx == 0)
            {
                if(n->is(node_type_t::PERCENT))
                {
                    dimension = "%";
                }
                else
                {
                    dimension = n->get_string();
                }
            }
            else
            {
                bool valid_dimension(true);
                if(n->is(node_type_t::PERCENT))
                {
                    valid_dimension = dimension == "%";
                }
                else
                {
                    valid_dimension = dimension == n->get_string();
                }
                if(!valid_dimension)
                {
                    // all dimensions must be the same
                    error::instance() << f_current->get_position()
                            << "min() expects all numbers to have the same dimension."
                            << error_mode_t::ERROR_ERROR;
                }
            }
            if(idx == 0 || r < minimum)
            {
                number = n;
                minimum = r;
            }
        }
        else
        {
            error::instance() << f_current->get_position()
                    << "min() expects any number of numbers."
                    << error_mode_t::ERROR_ERROR;
        }
    }
    return number;
}

node::pointer_t expression::internal_function__not(node::pointer_t func)
{
    // not(boolean)
    node::pointer_t arg1(func->get_child(0));
    if(arg1->size() != 1)
    {
        error::instance() << f_current->get_position()
                << "not() expects a boolean as its first argument."
                << error_mode_t::ERROR_ERROR;
        return node::pointer_t();
    }
    else
    {
        bool const r(boolean(arg1->get_child(0)));
        node::pointer_t result(new node(node_type_t::BOOLEAN, func->get_position()));
        result->set_boolean(!r); // this is 'not()' so false is true and vice versa
        return result;
    }
}

node::pointer_t expression::internal_function__percentage(node::pointer_t func)
{
    // percentage(expr)
    node::pointer_t any(internal_function__get_any(func, 0));
    if(any)
    {
        switch(any->get_type())
        {
        case node_type_t::DECIMAL_NUMBER:
            {
                node::pointer_t number(new node(node_type_t::PERCENT, any->get_position()));
                number->set_decimal_number(any->get_decimal_number());
                return number;
            }

        case node_type_t::PERCENT:
            // already a percentage, return as is
            return any;

        case node_type_t::INTEGER:
            {
                node::pointer_t number(new node(node_type_t::PERCENT, any->get_position()));
                number->set_decimal_number(any->get_integer());
                return number;
            }

        case node_type_t::IDENTIFIER:
        case node_type_t::STRING:
        case node_type_t::URL:
            {
                std::stringstream ss;
                ss << any->get_string();
                lexer l(ss, any->get_position());
                node::pointer_t number(l.next_token());
                if(number->is(node_type_t::WHITESPACE))
                {
                    number = l.next_token();
                }
                switch(number->get_type())
                {
                case node_type_t::DECIMAL_NUMBER:
                    {
                        node::pointer_t result(new node(node_type_t::PERCENT, any->get_position()));
                        result->set_decimal_number(number->get_decimal_number());
                        return result;
                    }

                case node_type_t::PERCENT:
                    return number;

                case node_type_t::INTEGER:
                    {
                        node::pointer_t result(new node(node_type_t::PERCENT, any->get_position()));
                        result->set_decimal_number(number->get_integer());
                        return result;
                    }

                default:
                    break;

                }
            }
            error::instance() << f_current->get_position()
                    << "percentage() expects a string parameter to represent a valid integer, decimal number, or percent value."
                    << error_mode_t::ERROR_ERROR;
            return node::pointer_t();

        default:
            break;

        }
    }

    error::instance() << f_current->get_position()
            << "percentage() expects one value as parameter."
            << error_mode_t::ERROR_ERROR;

    return node::pointer_t();
}

node::pointer_t expression::internal_function__red(node::pointer_t func)
{
    // red(color)
    color c;
    node::pointer_t col(internal_function__get_color(func, 0, c));
    if(col)
    {
        color_component_t r;
        color_component_t g;
        color_component_t b;
        color_component_t a;
        c.get_color(r, g, b, a);
        node::pointer_t component(new node(node_type_t::INTEGER, func->get_position()));
        component->set_integer(static_cast<integer_t>(r * 255.0 + 0.5));
        return component;
    }

    error::instance() << f_current->get_position()
            << "red() expects a color as parameter."
            << error_mode_t::ERROR_ERROR;

    return node::pointer_t();
}

node::pointer_t expression::internal_function__rgb(node::pointer_t func)
{
    // rgb(color)
    // rgb(red, green, blue)
    color c;
    node::pointer_t col(internal_function__get_color(func, 0, c));
    if(col)
    {
        // force alpha to 1.0
        color_component_t r;
        color_component_t g;
        color_component_t b;
        color_component_t a;
        c.get_color(r, g, b, a);
        c.set_color(r, g, b, static_cast<color_component_t>(1.0));
        col->set_color(c);
        return col;
    }
    else
    {
        decimal_number_t r;
        decimal_number_t g;
        decimal_number_t b;

        node::pointer_t col1(internal_function__get_number(func, 0, r));
        node::pointer_t col2(internal_function__get_number(func, 1, g));
        node::pointer_t col3(internal_function__get_number(func, 2, b));

        if(col1 && col2 && col3)
        {
            // force alpha to 1.0
            c.set_color(r / 255.0, g / 255.0, b / 255.0, 1.0);
            col.reset(new node(node_type_t::COLOR, func->get_position()));
            col->set_color(c);
            return col;
        }
    }

    error::instance() << f_current->get_position()
            << "rgb() expects exactly one color parameter (Color) or three numbers (Red, Green, Blue)."
            << error_mode_t::ERROR_ERROR;

    return node::pointer_t();
}

node::pointer_t expression::internal_function__rgba(node::pointer_t func)
{
    // rgba(color, alpha)
    // rgba(red, green, blue, alpha)
    color c;
    decimal_number_t a;
    node::pointer_t col(internal_function__get_color(func, 0, c));
    node::pointer_t alpha(internal_function__get_number(func, 1, a));
    if(col && alpha)
    {
        // replace alpha
        color_component_t r;
        color_component_t g;
        color_component_t b;
        color_component_t old_a;
        c.get_color(r, g, b, old_a);
        c.set_color(r, g, b, static_cast<color_component_t>(a));
        col->set_color(c);
        return col;
    }
    else
    {
        decimal_number_t r;
        decimal_number_t g;
        decimal_number_t b;

        node::pointer_t col1(internal_function__get_number(func, 0, r));
        node::pointer_t col2(internal_function__get_number(func, 1, g));
        node::pointer_t col3(internal_function__get_number(func, 2, b));
        node::pointer_t col4(internal_function__get_number(func, 3, a));

        if(col1 && col2 && col3 && col4)
        {
            // set color with alpha
            c.set_color(r / 255.0, g / 255.0, b / 255.0, a);
            col.reset(new node(node_type_t::COLOR, func->get_position()));
            col->set_color(c);
            return col;
        }
    }

    error::instance() << f_current->get_position()
            << "rgba() expects exactly one color parameter followed by alpha (Color, Alpha) or four numbers (Red, Green, Blue, Alpha)."
            << error_mode_t::ERROR_ERROR;

    return node::pointer_t();
}

node::pointer_t expression::internal_function__random(node::pointer_t func)
{
    // random()
    node::pointer_t number(new node(node_type_t::DECIMAL_NUMBER, func->get_position()));
    // rand() is certainly the worst function ever, but I am not even
    // sure why anyone would ever want to use random() in a CSS document
    // (frankly?! random CSS???)
    number->set_decimal_number(static_cast<decimal_number_t>(rand()) / (static_cast<decimal_number_t>(RAND_MAX) + 1.0));
    return number;
}

node::pointer_t expression::internal_function__round(node::pointer_t func)
{
    // round(number)
    decimal_number_t n;
    node::pointer_t number(internal_function__get_number(func, 0, n));
    if(number)
    {
        if(number->is(node_type_t::DECIMAL_NUMBER))
        {
            number->set_decimal_number(round(number->get_decimal_number()));
        }
        return number;
    }

    error::instance() << f_current->get_position()
            << "round() expects a number as parameter."
            << error_mode_t::ERROR_ERROR;

    return node::pointer_t();
}

node::pointer_t expression::internal_function__saturation(node::pointer_t func)
{
    // saturation(color)
    color c;
    node::pointer_t col(internal_function__get_color(func, 0, c));
    if(col)
    {
        color_component_t hue;
        color_component_t saturation;
        color_component_t lightness;
        color_component_t a;
        c.get_hsl(hue, saturation, lightness, a);
        node::pointer_t component(new node(node_type_t::PERCENT, func->get_position()));
        component->set_decimal_number(saturation);
        return component;
    }

    error::instance() << f_current->get_position()
            << "saturation() expects a color as parameter."
            << error_mode_t::ERROR_ERROR;

    return node::pointer_t();
}

node::pointer_t expression::internal_function__sign(node::pointer_t func)
{
    // sign(number)
    decimal_number_t n;
    node::pointer_t number(internal_function__get_number_or_percent(func, 0, n));
    if(number)
    {
        if(number->is(node_type_t::INTEGER))
        {
            number->set_integer(n < 0.0 ? -1 : (n > 0.0 ? 1 : 0));
        }
        else
        {
            number->set_decimal_number(n < 0.0 ? -1.0 : (n > 0.0 ? 1.0 : 0.0));
        }
        return number;
    }

    error::instance() << f_current->get_position()
            << "sign() expects a number as parameter."
            << error_mode_t::ERROR_ERROR;

    return node::pointer_t();
}

node::pointer_t expression::internal_function__sin(node::pointer_t func)
{
    // sin(number)
    decimal_number_t n;
    node::pointer_t number(internal_function__get_number(func, 0, n));
    if(number)
    {
        std::string const dimension(number->get_string());
        if(number->is(node_type_t::INTEGER))
        {
            number.reset(new node(node_type_t::DECIMAL_NUMBER, number->get_position()));
        }
        else
        {
            // we "lose" the dimension
            number->set_string("");
        }
        n = dimension_to_radians(func->get_position(), n, dimension);
        number->set_decimal_number(sin(n));
        return number;
    }

    error::instance() << f_current->get_position()
            << "sin() expects an angle as parameter."
            << error_mode_t::ERROR_ERROR;

    return node::pointer_t();
}

node::pointer_t expression::internal_function__sqrt(node::pointer_t func)
{
    // sqrt(number)
    decimal_number_t n;
    node::pointer_t number(internal_function__get_number(func, 0, n));
    if(number)
    {
        if(n < 0.0)
        {
            // an error occured, we cannot handle those dimensions
            error::instance() << f_current->get_position()
                    << "sqrt() expects zero or a positive number."
                    << error_mode_t::ERROR_ERROR;

            return node::pointer_t();
        }
        std::string dimension(number->get_string());
        if(!dimension.empty())
        {
            // the dimension MUST be a square
            //
            // first get the current dimensions
            dimension_vector_t dividend;
            dimension_vector_t divisor;
            dimensions_to_vectors(number->get_position(), dimension, dividend, divisor);

            if(((dividend.size() & 1) == 0)
            && ((divisor.size() & 1) == 0))
            {
                dimension_vector_t new_dividend;
                while(dividend.size() > 0)
                {
                    std::string const dim(*(dividend.end() - 1));
                    // remove this instance
                    dividend.erase(dividend.end() - 1);
                    // make sure there is another instance
                    dimension_vector_t::iterator it(std::find(dividend.begin(), dividend.end(), dim));
                    if(it == dividend.end())
                    {
                        // it's not valid
                        dimension.clear();
                        break;
                    }
                    // remove the copy instance
                    dividend.erase(it);
                    // keep one instance here instead
                    new_dividend.push_back(dim);
                }

                dimension_vector_t new_divisor;
                if(!dimension.empty())
                {
                    while(divisor.size() > 0)
                    {
                        std::string const dim(*(divisor.end() - 1));
                        // remove this instance
                        divisor.erase(divisor.end() - 1);
                        // make sure there is another instance
                        dimension_vector_t::iterator it(std::find(divisor.begin(), divisor.end(), dim));
                        if(it == divisor.end())
                        {
                            // it's not valid
                            dimension.clear();
                            break;
                        }
                        // remove the copy instance
                        divisor.erase(it);
                        // keep one instance here instead
                        new_divisor.push_back(dim);
                    }
                }

                if(!dimension.empty())
                {
                    dimension = rebuild_dimension(new_dividend, new_divisor);
                }
            }
            else
            {
                dimension.clear();
            }

            if(dimension.empty())
            {
                // an error occured, we cannot handle those dimensions
                error::instance() << f_current->get_position()
                        << "sqrt() expects dimensions to be squarely defined (i.e. 'px * px')."
                        << error_mode_t::ERROR_ERROR;

                return node::pointer_t();
            }
        }
        if(number->is(node_type_t::INTEGER))
        {
            number.reset(new node(node_type_t::DECIMAL_NUMBER, number->get_position()));
        }
        number->set_decimal_number(sqrt(n));
        number->set_string(dimension);
        return number;
    }

    error::instance() << f_current->get_position()
            << "sqrt() expects a number as parameter."
            << error_mode_t::ERROR_ERROR;

    return node::pointer_t();
}

node::pointer_t expression::internal_function__string(node::pointer_t func)
{
    // string(expr)
    node::pointer_t any(internal_function__get_any(func, 0));
    if(any)
    {
        switch(any->get_type())
        {
        case node_type_t::STRING:
            // already a string, return as is
            return any;

        case node_type_t::COLOR:
        case node_type_t::DECIMAL_NUMBER:
        case node_type_t::INTEGER:
        case node_type_t::PERCENT:
            {
                node::pointer_t id(new node(node_type_t::STRING, any->get_position()));
                id->set_string(any->to_string(0));
                return id;
            }

        case node_type_t::IDENTIFIER:
        case node_type_t::URL:
            {
                node::pointer_t id(new node(node_type_t::STRING, any->get_position()));
                id->set_string(any->get_string());
                return id;
            }

        default:
            break;

        }
    }

    error::instance() << f_current->get_position()
            << "string() expects one value as parameter."
            << error_mode_t::ERROR_ERROR;

    return node::pointer_t();
}

node::pointer_t expression::internal_function__str_length(node::pointer_t func)
{
    // str_length(string)
    std::string copy;
    node::pointer_t str(internal_function__get_string(func, 0, copy));
    if(str)
    {
        // make sure to compute the proper UTF-8 length
        node::pointer_t length(new node(node_type_t::INTEGER, func->get_position()));
        size_t l(0);
        for(char const *s(copy.c_str()); *s != '\0'; ++s)
        {
            if(static_cast<unsigned char>(*s) < 0x80
            || static_cast<unsigned char>(*s) >= 0xC0)
            {
                ++l;
            }
        }
        length->set_integer(l);
        return length;
    }

    error::instance() << f_current->get_position()
            << "str_length() expects one string as parameter."
            << error_mode_t::ERROR_ERROR;

    return node::pointer_t();
}

node::pointer_t expression::internal_function__tan(node::pointer_t func)
{
    // tan(number)
    decimal_number_t n;
    node::pointer_t number(internal_function__get_number(func, 0, n));
    if(number)
    {
        std::string const dimension(number->get_string());
        if(number->is(node_type_t::INTEGER))
        {
            number.reset(new node(node_type_t::DECIMAL_NUMBER, number->get_position()));
        }
        else
        {
            // we "lose" the dimension
            number->set_string("");
        }
        n = dimension_to_radians(func->get_position(), n, dimension);
        number->set_decimal_number(tan(n));
        return number;
    }

    error::instance() << f_current->get_position()
            << "tan() expects an angle as parameter."
            << error_mode_t::ERROR_ERROR;

    return node::pointer_t();
}

node::pointer_t expression::internal_function__unique_id(node::pointer_t func)
{
    // unique_id()
    // unique_id(identifier)
    std::string id;
    if(func->size() == 1)
    {
        node::pointer_t user_id(internal_function__get_string_or_identifier(func, 0, id));
        if(!user_id)
        {
            error::instance() << f_current->get_position()
                    << "unique_id() expects a string or an identifier as its optional parameter."
                    << error_mode_t::ERROR_ERROR;
            return node::pointer_t();
        }
        id = user_id->get_string();
    }
    if(id.empty())
    {
        id = "_csspp_unique";
    }

    // counter increases on each call
    // (this is not too good if the library is used by a GUI)
    ++g_unique_id_counter;

    id += std::to_string(g_unique_id_counter);

    node::pointer_t identifier(new node(node_type_t::IDENTIFIER, func->get_position()));
    identifier->set_string(id);

    return identifier;
}

node::pointer_t expression::internal_function__type_of(node::pointer_t func)
{
    // type_of(expression)
    node::pointer_t any(internal_function__get_any(func, 0));
    if(any)
    {
        node::pointer_t type(new node(node_type_t::STRING, func->get_position()));
        switch(any->get_type())
        {
        case node_type_t::ARRAY:
        case node_type_t::LIST:
            type->set_string("list");
            break;

        case node_type_t::BOOLEAN:
            type->set_string("bool");
            break;

        case node_type_t::COLOR:
            type->set_string("color");
            break;

        case node_type_t::DECIMAL_NUMBER:
        case node_type_t::PERCENT:
            type->set_string("number");
            break;

        case node_type_t::IDENTIFIER:
            type->set_string("identifier");
            break;

        case node_type_t::INTEGER:
            type->set_string("integer");
            break;

        case node_type_t::MAP:
            type->set_string("map");
            break;

        case node_type_t::STRING:
            type->set_string("string");
            break;

        case node_type_t::UNICODE_RANGE:
            type->set_string("unicode-range");
            break;

        //case node_type_t::NULL_TOKEN: -- null is like undefined
        default:
            type->set_string("undefined");
            break;

        }
        return type;
    }

    error::instance() << f_current->get_position()
            << "type_of() expects one value as a parameter."
            << error_mode_t::ERROR_ERROR;

    return node::pointer_t();
}

node::pointer_t expression::internal_function__unit(node::pointer_t func)
{
    // unit(number)
    decimal_number_t n;
    node::pointer_t number(internal_function__get_number_or_percent(func, 0, n));
    if(number != nullptr)
    {
        node::pointer_t unit(new node(node_type_t::STRING, func->get_position()));
        if(number->is(node_type_t::PERCENT))
        {
            unit->set_string("%");
        }
        else
        {
            unit->set_string(number->get_string());
        }
        return unit;
    }

    error::instance() << f_current->get_position()
            << "unit() expects a number as parameter."
            << error_mode_t::ERROR_ERROR;

    return node::pointer_t();
}

node::pointer_t expression::internal_function__variable_exists(node::pointer_t func)
{
    // variable_exists(name)
    std::string name;
    node::pointer_t id(internal_function__get_string_or_identifier(func, 0, name));
    if(id && !name.empty())
    {
        node::pointer_t result(new node(node_type_t::BOOLEAN, func->get_position()));

        // although variables were already applied they will still be
        // defined when we reach these lines of code
        if(f_variable_handler)
        {
            node::pointer_t var(f_variable_handler->get_variable(name, false));
            if(var
            && var->is(node_type_t::LIST)
            && !var->empty()
            && (var->get_child(0)->is(node_type_t::VARIABLE)      // $<name>
            || var->get_child(0)->is(node_type_t::IDENTIFIER)))   // @mixin <name>
            {
                result->set_boolean(true);
            }
        }
        // else -- default is already false

        return result;
    }

    error::instance() << f_current->get_position()
            << "variable_exists() expects a string or an identifier as parameter."
            << error_mode_t::ERROR_ERROR;

    return node::pointer_t();
}

node::pointer_t expression::excecute_function(node::pointer_t func)
{
    typedef node::pointer_t (expression::*internal_function_t)(node::pointer_t func);

    // TODO: maybe add a parser which takes all the ARGs and transform
    //       them in a list of ready to use nodes for our internal
    //       functions?
    struct function_table_t
    {
        char const *            f_name;
        int                     f_min_params;
        int                     f_max_params;
        internal_function_t     f_func;
    };

    static function_table_t const g_functions[] =
    {
        {
            "abs",
            1,
            1,
            &expression::internal_function__abs
        },
        {
            "acos",
            1,
            1,
            &expression::internal_function__acos
        },
        {
            "alpha",
            1,
            1,
            &expression::internal_function__alpha
        },
        {
            "asin",
            1,
            1,
            &expression::internal_function__asin
        },
        {
            "atan",
            1,
            1,
            &expression::internal_function__atan
        },
        {
            "blue",
            1,
            1,
            &expression::internal_function__blue
        },
        {
            "ceil",
            1,
            1,
            &expression::internal_function__ceil
        },
        {
            "cos",
            1,
            1,
            &expression::internal_function__cos
        },
        {
            "decimal_number",
            1,
            1,
            &expression::internal_function__decimal_number
        },
        {
            "floor",
            1,
            1,
            &expression::internal_function__floor
        },
        {
            "frgb",
            1,
            3,
            &expression::internal_function__frgb
        },
        {
            "frgba",
            2,
            4,
            &expression::internal_function__frgba
        },
        {
            "function_exists",
            1,
            1,
            &expression::internal_function__function_exists
        },
        {
            "global_variable_exists",
            1,
            1,
            &expression::internal_function__global_variable_exists
        },
        {
            "green",
            1,
            1,
            &expression::internal_function__green
        },
        {
            "hsl",
            3,
            3,
            &expression::internal_function__hsl
        },
        {
            "hsla",
            4,
            4,
            &expression::internal_function__hsla
        },
        {
            "hue",
            1,
            1,
            &expression::internal_function__hue
        },
        {
            "identifier",
            1,
            1,
            &expression::internal_function__identifier
        },
        {
            "if",
            3,
            3,
            &expression::internal_function__if
        },
        {
            "integer",
            1,
            1,
            &expression::internal_function__integer
        },
        {
            "inspect",
            1,
            1,
            &expression::internal_function__inspect
        },
        {
            "lightness",
            1,
            1,
            &expression::internal_function__lightness
        },
        {
            "log",
            1,
            1,
            &expression::internal_function__log
        },
        {
            "max",
            1,
            INT_MAX,
            &expression::internal_function__max
        },
        {
            "min",
            1,
            INT_MAX,
            &expression::internal_function__min
        },
        {
            "not",
            1,
            1,
            &expression::internal_function__not
        },
        {
            "percentage",
            1,
            1,
            &expression::internal_function__percentage
        },
        {
            "random",
            0,
            0,
            &expression::internal_function__random
        },
        {
            "red",
            1,
            1,
            &expression::internal_function__red
        },
        {
            "rgb",
            1,
            3,
            &expression::internal_function__rgb
        },
        {
            "rgba",
            2,
            4,
            &expression::internal_function__rgba
        },
        {
            "round",
            1,
            1,
            &expression::internal_function__round
        },
        {
            "saturation",
            1,
            1,
            &expression::internal_function__saturation
        },
        {
            "sign",
            1,
            1,
            &expression::internal_function__sign
        },
        {
            "sin",
            1,
            1,
            &expression::internal_function__sin
        },
        {
            "sqrt",
            1,
            1,
            &expression::internal_function__sqrt
        },
        {
            "string",
            1,
            1,
            &expression::internal_function__string
        },
        {
            "str_length",
            1,
            1,
            &expression::internal_function__str_length
        },
        {
            "tan", // WARNING: just 'tan' is a color... 'tan(...)' is a function
            1,
            1,
            &expression::internal_function__tan
        },
        {
            "type_of",
            1,
            1,
            &expression::internal_function__type_of
        },
        {
            "unique_id",
            0,
            1,
            &expression::internal_function__unique_id
        },
        {
            "unit",
            1,
            1,
            &expression::internal_function__unit
        },
        {
            "variable_exists",
            1,
            1,
            &expression::internal_function__variable_exists
        }
    };

    std::string const function_name(func->get_string());

    // TODO: (1) verify that functions are properly sorted
    //       (2) use a binary search
    for(size_t idx(0); idx < sizeof(g_functions) / sizeof(g_functions[0]); ++idx)
    {
        if(function_name == g_functions[idx].f_name)
        {
            // found the function, it is internal!

            // right number of parameters?
            if(func->size() >= static_cast<size_t>(g_functions[idx].f_min_params)
            && func->size() <= static_cast<size_t>(g_functions[idx].f_max_params))
            {
                if(function_name == "function_exists")
                {
                    // first check whether the user is checking for an
                    // internal function! (because the list of internal
                    // functions is not available anywhere else)
                    std::string name;
                    node::pointer_t id(internal_function__get_string_or_identifier(func, 0, name));
                    if(id && !name.empty())
                    {
                        for(size_t j(0); j < sizeof(g_functions) / sizeof(g_functions[0]); ++j)
                        {
                            if(name == g_functions[j].f_name)
                            {
                                node::pointer_t result(new node(node_type_t::BOOLEAN, func->get_position()));
                                result->set_boolean(true);
                                return result;
                            }
                        }
                    }
                }
                return (this->*g_functions[idx].f_func)(func);
            }

            if(g_functions[idx].f_min_params == g_functions[idx].f_max_params)
            {
                error::instance() << f_current->get_position()
                        << function_name
                        << "() expects exactly "
                        << g_functions[idx].f_min_params
                        << " parameter"
                        << (g_functions[idx].f_min_params == 1 ? "" : "s")
                        << "."
                        << error_mode_t::ERROR_ERROR;
            }
            else
            {
                error::instance() << f_current->get_position()
                        << function_name
                        << "() expects between "
                        << g_functions[idx].f_min_params
                        << " and "
                        << g_functions[idx].f_max_params
                        << " parameters."
                        << error_mode_t::ERROR_ERROR;
            }
            return node::pointer_t();
        }
    }

    // if we have a handler then allow the handler to run to execute
    // user defined functions (@mixin func() ...)
    if(f_variable_handler)
    {
        return f_variable_handler->execute_user_function(func);
    }

    // "unknown" functions have to be left alone since these may be
    // CSS functions that we do not want to transform (we already
    // worked on their arguments, that's the extend of it at this point.)
    //
    // For now I mark it as unreachable because we should always be
    // using expression objects with a variable handler.
    return func;   // LCOV_EXCL_LINE
}

} // namespace csspp

// Local Variables:
// mode: cpp
// indent-tabs-mode: nil
// c-basic-offset: 4
// tab-width: 4
// End:

// vim: ts=4 sw=4 et
