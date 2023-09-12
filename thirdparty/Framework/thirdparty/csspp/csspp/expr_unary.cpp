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

void expression::compile_args(bool divide_font_metrics)
{
    if(!f_node->empty())
    {
        bool const end_with_bracket(f_node->get_last_child()->is(node_type_t::OPEN_CURLYBRACKET));
        size_t const max_children(f_node->size() - (end_with_bracket ? 1 : 0));
        for(size_t a(0); a < max_children; ++a)
        {
            expression arg_expr(f_node->get_child(a));
            arg_expr.set_variable_handler(f_variable_handler);
            arg_expr.f_divide_font_metrics = divide_font_metrics;
            arg_expr.compile_list(f_node);
        }
    }
}

node::pointer_t expression::compile_list(node::pointer_t parent)
{
    mark_start();
    next();
    node::pointer_t result;

    // result is a list: a b c ...
    for(;;)
    {
        // we have one special case here: !important cannot be
        // compiled as an expression...
        if(f_current->is(node_type_t::EXCLAMATION))
        {
            // this is viewed as !important and only such
            // can appear now so we can just return immediately
            if(!end_of_nodes())
            {
                error::instance() << f_current->get_position()
                        << "A special flag, !"
                        << f_current->get_string()
                        << " in this case, must only appear at the end of a declaration."
                        << error_mode_t::ERROR_WARNING;
            }

            // we remove the !<word> from the declaration and
            // setup a flag instead
            f_node->remove_child(f_current);
            if(f_pos > 0)
            {
                --f_pos;
            }
            parent->set_flag(f_current->get_string(), true);
        }
        else
        {
            result = conditional();
            replace_with_result(result);
        }
        if(end_of_nodes())
        {
            break;
        }
        next();
        // keep one whitespace between expressions if such exists
        if(f_current->is(node_type_t::WHITESPACE))
        {
            if(end_of_nodes())
            {
                // TODO: list of nodes ends with WHITESPACE
                break;    // LCOV_EXCL_LINE
            }
            mark_start();
            next();
        }
    }

    return result;
}

node::pointer_t expression::unary()
{
    // unary: IDENTIFIER
    //      | INTEGER
    //      | DECIMAL_NUMBER
    //      | EXCLAMATION
    //      | STRING
    //      | PERCENT
    //      | BOOLEAN
    //      | HASH (-> COLOR)
    //      | UNICODE_RANGE
    //      | URL
    //      | FUNCTION argument_list ')' -- including url()
    //      | '(' expression_list ')'
    //      | '+' power
    //      | '-' power

    switch(f_current->get_type())
    {
    case node_type_t::ARRAY:
    case node_type_t::BOOLEAN:
    case node_type_t::COLOR:
    case node_type_t::DECIMAL_NUMBER:
    case node_type_t::EXCLAMATION:  // this is not a BOOLEAN NOT operator...
    case node_type_t::INTEGER:
    case node_type_t::MAP:
    case node_type_t::NULL_TOKEN:
    case node_type_t::PERCENT:
    case node_type_t::STRING:
    case node_type_t::UNICODE_RANGE:
    case node_type_t::URL:
        {
            node::pointer_t result(f_current);
            // skip that token
            next();
            return result;
        }

    case node_type_t::FUNCTION:
        {
            node::pointer_t func(f_current);

            // skip the '<func>('
            next();

            // calculate the arguments
            parser::argify(func);
            if(func->get_string() != "calc"
            && func->get_string() != "expression")
            {
                expression args_expr(func);
                args_expr.set_variable_handler(f_variable_handler);
                args_expr.compile_args(false);
            }
            //else -- we may want to verify the calculations, but
            //        we cannot compile those

            return excecute_function(func);
        }

    case node_type_t::OPEN_PARENTHESIS:
        {
            // calculate the result of the sub-expression
            expression group(f_current);
            group.set_variable_handler(f_variable_handler);
            group.next();

            // skip the '(' in the main expression
            next();

            return group.expression_list();
        }

    case node_type_t::ADD:
        // completely ignore the '+' because we assume that
        // '+<anything>' <=> '<anything>'

        // skip the '+'
        next();
        return power();

    case node_type_t::SUBTRACT:
        {
            // skip the '-'
            next();

            node::pointer_t result(power());
            if(!result)
            {
                return node::pointer_t();
            }
            switch(result->get_type())
            {
            case node_type_t::INTEGER:
                result->set_integer(-result->get_integer());
                return result;

            case node_type_t::DECIMAL_NUMBER:
            case node_type_t::PERCENT:
                result->set_decimal_number(-result->get_decimal_number());
                return result;

            default:
                error::instance() << f_current->get_position()
                        << "unsupported type "
                        << result->get_type()
                        << " for operator '-'."
                        << error_mode_t::ERROR_ERROR;
                return node::pointer_t();

            }
        }

    // This is not too good, we actually transform the !important in
    // one 'EXCLAMATION + string' node; use the not(...) instead
    //case node_type_t::EXCLAMATION:
    //    {
    //        // skip the '!'
    //        next();
    //        node::pointer_t result(power());
    //        bool const r(boolean(result));
    //        // make sure the result is a boolean
    //        if(!result->is(node_type_t::BOOLEAN))
    //        {
    //            result.reset(new node(node_type_t::BOOLEAN, result->get_position()));
    //        }
    //        result->set_boolean(!r);
    //        return result;
    //    }

    case node_type_t::HASH:
        // a '#...' in an expression is expected to be a valid color
        {
            color hash;
            if(!hash.set_color(f_current->get_string(), false))
            {
                error::instance() << f_current->get_position()
                        << "the color in #"
                        << f_current->get_string()
                        << " is not valid."
                        << error_mode_t::ERROR_ERROR;

                // skip the HASH
                next();
                return node::pointer_t();
            }
            node::pointer_t color_node(new node(node_type_t::COLOR, f_current->get_position()));
            color_node->set_color(hash);

            // skip the HASH
            next();
            return color_node;
        }

    case node_type_t::IDENTIFIER:
        // an identifier may represent a color, null, true, or false
        {
            node::pointer_t result(f_current);
            // skip the IDENTIFIER
            next();

            std::string const identifier(result->get_string());

            // an internally recognized identifier? (null, true, false)
            if(identifier == "null")
            {
                return node::pointer_t(new node(node_type_t::NULL_TOKEN, result->get_position()));
            }
            if(identifier == "true")
            {
                node::pointer_t b(new node(node_type_t::BOOLEAN, result->get_position()));
                b->set_boolean(true);
                return b;
            }
            if(identifier == "false")
            {
                // a boolean is false by default, so no need to set the value
                return node::pointer_t(new node(node_type_t::BOOLEAN, result->get_position()));
            }

            // a color?
            color col;
            if(col.set_color(identifier, true))
            {
                node::pointer_t color_node(new node(node_type_t::COLOR, result->get_position()));
                color_node->set_color(col);

                return color_node;
            }

            // an expression variable?
            auto var(f_variables.find(identifier));
            if(var != f_variables.end())
            {
                return var->second;
            }

            // it is not a color or a variable, return as is
            return result;
        }

    default:
        error::instance() << f_current->get_position()
                << "unsupported type "
                << f_current->get_type()
                << " as a unary expression token."
                << error_mode_t::ERROR_ERROR;
        return node::pointer_t();

    }
    /*NOTREACHED*/
}

} // namespace csspp

// Local Variables:
// mode: cpp
// indent-tabs-mode: nil
// c-basic-offset: 4
// tab-width: 4
// End:

// vim: ts=4 sw=4 et
