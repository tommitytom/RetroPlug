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

bool expression::is_label() const
{
    // we have a label if we have:
    //    <identifier> <ws>* ':'
    if(!f_current->is(node_type_t::IDENTIFIER)
    || f_pos >= f_node->size())
    {
        return false;
    }

    return f_node->get_child(f_pos)->is(node_type_t::COLON);
}

node::pointer_t expression::expression_list()
{
    // expression-list: array
    //                | map
    //
    // array: assignment
    //      | array ',' assignment
    //
    // map: IDENTIFIER ':' assignment
    //    | IDENTIFIER ':'
    //    | map ',' IDENTIFIER ':' assignment
    //    | map ',' IDENTIFIER ':'
    //

    if(is_label())
    {
        node::pointer_t map(new node(node_type_t::MAP, f_current->get_position()));
        bool found_end(false);
        while(!found_end && is_label())
        {
            node::pointer_t name(f_current);

            // skip the IDENTIFIER (f_current == ':')
            next();

            // skip the ':'
            next();

            node::pointer_t result;
            if(f_current->is(node_type_t::COMMA))
            {
                // empty entries are viewed as valid and set to NULL
                // (see below for the NULL_TOKEN allocation)

                // skip the ','
                next();
            }
            else if(f_current->is(node_type_t::EOF_TOKEN))
            {
                // map ends with just a label, make sure we add a NULL too
                result.reset();
            }
            else
            {
                result = assignment();

                if(f_current->is(node_type_t::COMMA))
                {
                    next();
                }
                else
                {
                    // no comma, we must have reached the end of the list
                    found_end = true;
                }
            }

            if(!result)
            {
                // maps need to have an even number of entries, but
                // the value of an entry does not need to be provided
                // in which case we want to put NULL in there
                result.reset(new node(node_type_t::NULL_TOKEN, f_current->get_position()));
            }

            // add both at the same time
            map->add_child(name);
            map->add_child(result);
        }
        return map;
    }
    else
    {
        node::pointer_t result(assignment());

        if(result
        && f_current->is(node_type_t::COMMA))
        {
            // in CSS Preprocessor, a list of expressions is an ARRAY
            // (contrary to C/C++ which just return the last expression)
            node::pointer_t array(new node(node_type_t::ARRAY, f_current->get_position()));
            array->add_child(result);

            while(f_current->is(node_type_t::COMMA))
            {
                // skip the ','
                next();

                result = assignment();
                if(!result)
                {
                    break;
                }
                array->add_child(result);
            }

            // the result is the array in this case
            return array;
        }

        return result;
    }
}

node::pointer_t expression::assignment()
{
    // assignment: conditional
    //           | IDENTIFIER ':=' conditional

    node::pointer_t result(conditional());
    if(!result)
    {
        return node::pointer_t();
    }

    if(result->is(node_type_t::IDENTIFIER)
    && f_current->is(node_type_t::ASSIGNMENT))
    {
        next();

        node::pointer_t value(conditional());
        f_variables[result->get_string()] = value;

        // the return value of an assignment is the value of
        // the variable
        result = value;
    }

    return result;
}

node::pointer_t expression::post()
{
    // post: unary
    //     | post '[' expression ']'
    //     | post '.' IDENTIFIER

    // TODO: add support to access color members (i.e. $c.red <=> red($c))

    node::pointer_t result(unary());
    if(!result)
    {
        return node::pointer_t();
    }

    node::pointer_t index;
    for(;;)
    {
        if(f_current->is(node_type_t::OPEN_SQUAREBRACKET))
        {
            // compile the index expression
            expression index_expr(f_current);
            index_expr.set_variable_handler(f_variable_handler);
            index_expr.next();
            node::pointer_t i(index_expr.expression_list());
            if(!i)
            {
                return node::pointer_t();
            }

            // skip the '['
            next();

            if(i->is(node_type_t::INTEGER))
            {
                if(result->is(node_type_t::ARRAY)
                || result->is(node_type_t::LIST))
                {
                    // index is 1 based (not like in C/C++)
                    integer_t idx(i->get_integer());
                    if(idx < 0)
                    {
                        // negative numbers get items from the item
                        idx = result->size() + idx;
                    }
                    else
                    {
                        --idx;
                    }
                    if(static_cast<size_t>(idx) >= result->size())
                    {
                        error::instance() << f_current->get_position()
                                << "index "
                                << i->get_integer()
                                << " is out of range. The allowed range is 1 to "
                                << static_cast<int>(result->size())
                                << "."
                                << error_mode_t::ERROR_ERROR;
                        return node::pointer_t();
                    }
                    result = result->get_child(idx);
                }
                else if(result->is(node_type_t::MAP))
                {
                    // index is 1 based (not like in C/C++)
                    // maps are defined as <property name> ':' <property value>
                    // so the numeric index being used to access the property
                    // value it has to be x 2 + 1 (C index: 1, 3, 5...)
                    // if negative we first have to "invert" the index
                    integer_t idx(i->get_integer());
                    if(idx < 0)
                    {
                        // negative numbers get items from the item
                        idx = result->size() / 2 + idx;
                    }
                    else
                    {
                        --idx;
                    }
                    idx = idx * 2 + 1;
                    if(static_cast<size_t>(idx) >= result->size())
                    {
                        error::instance() << f_current->get_position()
                                << "index "
                                << i->get_integer()
                                << " is out of range. The allowed range is 1 to "
                                << static_cast<int>(result->size()) / 2
                                << "."
                                << error_mode_t::ERROR_ERROR;
                        return node::pointer_t();
                    }
                    result = result->get_child(idx);
                }
                else
                {
                    error::instance() << f_current->get_position()
                            << "unsupported type "
                            << result->get_type()
                            << " for the 'array[<index>]' operation."
                            << error_mode_t::ERROR_ERROR;
                    return node::pointer_t();
                }
            }
            else if(i->is(node_type_t::STRING)
                 || i->is(node_type_t::IDENTIFIER))
            {
                // nothing more to skip, the string is a child in
                // a separate list
                index = i;
                goto field_index;
            }
            else
            {
                error::instance() << f_current->get_position()
                        << "an integer, an identifier, or a string was expected as the index (defined in '[ ... ]'). A "
                        << i->get_type()
                        << " was not expected."
                        << error_mode_t::ERROR_ERROR;
                return node::pointer_t();
            }
        }
        else if(f_current->is(node_type_t::PERIOD))
        {
            // skip the '.'
            next();

            if(!f_current->is(node_type_t::IDENTIFIER))
            {
                error::instance() << f_current->get_position()
                        << "only an identifier is expected after a '.'."
                        << error_mode_t::ERROR_ERROR;
                return node::pointer_t();
            }
            index = f_current;

            // skip the index (identifier)
            next();

field_index:
            if(result->is(node_type_t::MAP))
            {
                // in this case the index is a string or an identifier
                std::string const idx(index->get_string());
                size_t const max_item(result->size());
                if((max_item & 1) != 0)
                {
                    throw csspp_exception_logic("expression.cpp:expression::post(): number of items in a map has to be even."); // LCOV_EXCL_LINE
                }
                bool found(false);
                for(size_t j(0); j < max_item; j += 2)
                {
                    node::pointer_t item_name(result->get_child(j));
                    if(!item_name->is(node_type_t::IDENTIFIER))
                    {
                        throw csspp_exception_logic("expression.cpp:expression::post(): a map has the name of an entry which is not an identifier."); // LCOV_EXCL_LINE
                    }
                    if(item_name->get_string() == idx)
                    {
                        result = result->get_child(j + 1);
                        found = true;
                        break;
                    }
                }
                if(!found)
                {
                    // TBD: should this be acceptable and we return NULL instead?
                    error::instance() << f_current->get_position()
                            << "'map[\""
                            << idx
                            << "\"]' is not set."
                            << error_mode_t::ERROR_ERROR;
                    return node::pointer_t();
                }
            }
            else
            {
                error::instance() << f_current->get_position()
                        << "unsupported left handside type "
                        << result->get_type()
                        << " for the '<map>.<identifier>' operation."
                        << error_mode_t::ERROR_ERROR;
                return node::pointer_t();
            }
        }
        else
        {
            break;
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
