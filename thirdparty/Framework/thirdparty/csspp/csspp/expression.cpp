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

expression::expression(node::pointer_t n)
    : f_node(n)
{
    if(!f_node)
    {
        throw csspp_exception_logic("expression.cpp:expression(): contructor called with a null pointer.");
    }
}

void expression::set_variable_handler(expression_variables_interface * handler)
{
    f_variable_handler = handler;
}

node::pointer_t expression::compile()
{
    mark_start();
    next();
    return replace_with_result(conditional());
}

// basic state handling
bool expression::end_of_nodes()
{
    return f_pos >= f_node->size();
}

void expression::mark_start()
{
    f_start = f_pos;
}

node::pointer_t expression::replace_with_result(node::pointer_t result)
{
    if(result)
    {
        if(f_start == static_cast<size_t>(-1))
        {
            throw csspp_exception_logic("expression.cpp:expression(): replace_with_result() cannot be called if mark_start() was never called."); // LCOV_EXCL_LINE
        }

        // f_pos may point to a tag right after the end of the previous
        // expression; expressions may be separated by WHITESPACE tokens
        // too so we have to restore them if they appear at the end of
        // the epxression we just worked on (i.e. we cannot eat a WHITESPACE
        // at the end of an expression.)
        if(!f_current->is(node_type_t::EOF_TOKEN) && f_pos > 0)
        {
            while(f_pos > 0)
            {
                --f_pos;
                if(f_node->get_child(f_pos) == f_current)
                {
                    break;
                }
            }
            if(f_pos > 0 && f_node->get_child(f_pos - 1)->is(node_type_t::WHITESPACE))
            {
                --f_pos;
            }
        }

        // this "reduces" the expression with its result
        while(f_pos > f_start)
        {
            --f_pos;
            f_node->remove_child(f_pos);
        }
        f_node->insert_child(f_pos, result);
        ++f_pos;
    }

    mark_start();

    return result;
}

void expression::next()
{
    if(f_pos >= f_node->size())
    {
        if(!f_current
        || !f_current->is(node_type_t::EOF_TOKEN))
        {
            f_current.reset(new node(node_type_t::EOF_TOKEN, f_node->get_position()));
        }
    }
    else
    {
        f_current = f_node->get_child(f_pos);
        ++f_pos;
        while(f_pos < f_node->size()
           && f_node->get_child(f_pos)->is(node_type_t::WHITESPACE))
        {
            ++f_pos;
        }
    }
}

} // namespace csspp

// Local Variables:
// mode: cpp
// indent-tabs-mode: nil
// c-basic-offset: 4
// tab-width: 4
// End:

// vim: ts=4 sw=4 et
