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
 * \brief Implementation of the CSS Preprocessor node.
 *
 * The CSS Preprocessor node handles the tree of nodes that the parser
 * generates, the compiler crunches, and the assembler outputs.
 *
 * All the code that handles the nodes is found here, however, the
 * compiler and expression classes handle the various operations that
 * are required between nodes. The nodes are nearly only limited to
 * handling the data they hold and the tree.
 *
 * \sa \ref lexer_rules
 */

// self
//
#include    <csspp/node.h>

#include    <csspp/exception.h>
#include    <csspp/nth_child.h>
#include    <csspp/unicode_range.h>


// C++
//
#include    <algorithm>
#include    <iostream>


// last include
//
//#include    <snapdev/poison.h>



namespace csspp
{

// make sure we have a copy (catch makes it a requirement probably because
// of template use)
size_t const node::npos;

namespace
{

uint32_t g_node_count = 0;
uint32_t g_node_max_count = 0;

union convert_t
{
    integer_t           f_int;
    decimal_number_t    f_flt;
};

void type_supports_integer(node_type_t const type)
{
    switch(type)
    {
    case node_type_t::AN_PLUS_B:
    case node_type_t::ARG:
    case node_type_t::AT_KEYWORD:
    case node_type_t::COMMENT:
    case node_type_t::INTEGER:
    case node_type_t::UNICODE_RANGE:
        break;

    default:
        {
            std::stringstream ss;
            ss << "trying to access (read/write) the integer of a node of type " << type << ", which does not support integers.";
            throw csspp_exception_logic(ss.str());
        }

    }
}

void type_supports_boolean(node_type_t const type)
{
    switch(type)
    {
    case node_type_t::BOOLEAN:
    case node_type_t::DECIMAL_NUMBER:
    case node_type_t::INTEGER:
    case node_type_t::OPEN_CURLYBRACKET:
    case node_type_t::PERCENT:
        break;

    default:
        {
            std::stringstream ss;
            ss << "trying to access (read/write) the boolean of a node of type " << type << ", which does not support booleans.";
            throw csspp_exception_logic(ss.str());
        }

    }
}

void type_supports_decimal_number(node_type_t const type)
{
    switch(type)
    {
    case node_type_t::DECIMAL_NUMBER:
    case node_type_t::PERCENT:
    case node_type_t::FRAME:
        break;

    default:
        {
            std::stringstream ss;
            ss << "trying to access (read/write) the decimal number of a node of type " << type << ", which does not support decimal numbers.";
            throw csspp_exception_logic(ss.str());
        }

    }
}

void type_supports_string(node_type_t const type)
{
    switch(type)
    {
    case node_type_t::AT_KEYWORD:
    case node_type_t::COMMENT:
    case node_type_t::DECIMAL_NUMBER:
    case node_type_t::DECLARATION:
    case node_type_t::EXCLAMATION:
    case node_type_t::FUNCTION:
    case node_type_t::HASH:
    case node_type_t::IDENTIFIER:
    case node_type_t::INTEGER:
    case node_type_t::PLACEHOLDER:
    case node_type_t::STRING:
    case node_type_t::URL:
    case node_type_t::VARIABLE:
    case node_type_t::VARIABLE_FUNCTION:
        break;

    default:
        {
            std::stringstream ss;
            ss << "trying to access (read/write) the string of a node of type " << type << ", which does not support strings.";
            throw csspp_exception_logic(ss.str());
        }

    }
}

void type_supports_color(node_type_t const type)
{
    switch(type)
    {
    case node_type_t::COLOR:
        break;

    default:
        {
            std::stringstream ss;
            ss << "trying to access (read/write) the color of a node of type " << type << ", which does not support colors.";
            throw csspp_exception_logic(ss.str());
        }

    }
}

void type_supports_font_metrics(node_type_t const type)
{
    switch(type)
    {
    case node_type_t::FONT_METRICS:
        break;

    default:
        {
            std::stringstream ss;
            ss << "trying to access (read/write) the line height of a node of type " << type << ", which does not support line heights.";
            throw csspp_exception_logic(ss.str());
        }
    }
}

void type_supports_children(node_type_t const type)
{
    switch(type)
    {
    case node_type_t::ARG:
    case node_type_t::ARRAY:
    case node_type_t::AT_KEYWORD:
    case node_type_t::COMPONENT_VALUE:
    case node_type_t::DECLARATION:
    case node_type_t::FUNCTION:
    case node_type_t::LIST:
    case node_type_t::MAP:
    case node_type_t::OPEN_CURLYBRACKET:
    case node_type_t::OPEN_PARENTHESIS:
    case node_type_t::OPEN_SQUAREBRACKET:
    case node_type_t::VARIABLE_FUNCTION:
    case node_type_t::FRAME:
        break;

    default:
        {
            std::stringstream ss;
            ss << "trying to access (read/write) the children of a node of type " << type << ", which does not support children.";
            throw csspp_exception_logic(ss.str());
        }

    }
}

} // no name namespace

node::node(node_type_t const type, position const & pos)
    : f_type(type)
    , f_position(pos)
{
    ++g_node_count;
    if(g_node_max_count != 0
    && g_node_count >= g_node_max_count)
    {
        // This is NOT a bug per se, you may limit the number of nodes in case
        // you have a limited amount of memory available or you suspect the
        // CSS Preprocessor has a bug which allocates nodes forever; this is
        // used for our tests with a maximum number of nodes equal to one
        // million (which generally represents a lot less than 1Gb of RAM.)
        std::cerr << "error: node of type " << type << " cannot be allocated.\n";                                                           // LCOV_EXCL_LINE
        throw csspp_exception_overflow("node.cpp: node::node() too many nodes allocated at the same time, we are probably having a leak."); // LCOV_EXCL_LINE
    }
}

node::~node()
{
    --g_node_count;
}

node::pointer_t node::clone() const
{
    // create the clone
    pointer_t result(new node(f_type, f_position));

    // copy the other simple values
    result->f_boolean = f_boolean;
    result->f_integer = f_integer;
    result->f_decimal_number = f_decimal_number;
    result->f_string = f_string;
    result->f_flags = f_flags;

    for(auto c : f_children)
    {
        result->f_children.push_back(c->clone());
    }

    result->copy_variable(const_cast<node *>(this)->shared_from_this());

    return result;
}

node_type_t node::get_type() const
{
    return f_type;
}

bool node::is(node_type_t const type) const
{
    return f_type == type;
}

boolean_t node::to_boolean() const
{
    switch(f_type)
    {
    case node_type_t::BOOLEAN:
        return f_boolean ? boolean_t::BOOLEAN_TRUE : boolean_t::BOOLEAN_FALSE;

    case node_type_t::IDENTIFIER:
        if(f_string == "true")
        {
            return boolean_t::BOOLEAN_TRUE;
        }
        if(f_string == "false"
        || f_string == "null")
        {
            return boolean_t::BOOLEAN_FALSE;
        }
        return boolean_t::BOOLEAN_INVALID;

    case node_type_t::INTEGER:
        return f_integer != 0 ? boolean_t::BOOLEAN_TRUE : boolean_t::BOOLEAN_FALSE;

    case node_type_t::DECIMAL_NUMBER:
    case node_type_t::PERCENT:
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
        return f_decimal_number != 0.0 ? boolean_t::BOOLEAN_TRUE : boolean_t::BOOLEAN_FALSE;
#pragma GCC diagnostic pop

    case node_type_t::STRING:
        return f_string.empty() ? boolean_t::BOOLEAN_FALSE : boolean_t::BOOLEAN_TRUE;

    case node_type_t::ARRAY:
    case node_type_t::LIST:
    case node_type_t::MAP:
        return f_children.empty() ? boolean_t::BOOLEAN_FALSE : boolean_t::BOOLEAN_TRUE;

    case node_type_t::COLOR:
        {
            color c(get_color());
            return (c.get_color() & 0x00FFFFFF) == 0
                    ? boolean_t::BOOLEAN_FALSE
                    : boolean_t::BOOLEAN_TRUE;
        }
        break;

    case node_type_t::NULL_TOKEN:
        return boolean_t::BOOLEAN_FALSE;

    default:
        return boolean_t::BOOLEAN_INVALID;

    }
    /*NOTREACHED*/
}

position const & node::get_position() const
{
    return f_position;
}

std::string const & node::get_string() const
{
    type_supports_string(f_type);
    return f_string;
}

void node::set_string(std::string const & str)
{
    type_supports_string(f_type);
    f_string = str;
}

std::string const & node::get_lowercase_string() const
{
    type_supports_string(f_type);
    return f_lowercase_string;
}

void node::set_lowercase_string(std::string const & str)
{
    type_supports_string(f_type);
    f_lowercase_string = str;
}

integer_t node::get_integer() const
{
    type_supports_integer(f_type);
    return f_integer;
}

void node::set_integer(integer_t integer)
{
    type_supports_integer(f_type);
    f_integer = integer;
}

bool node::get_boolean() const
{
    type_supports_boolean(f_type);
    return f_boolean;
}

void node::set_boolean(bool boolean)
{
    type_supports_boolean(f_type);
    f_boolean = boolean;
}

decimal_number_t node::get_decimal_number() const
{
    type_supports_decimal_number(f_type);
    return f_decimal_number;
}

void node::set_decimal_number(decimal_number_t decimal_number)
{
    type_supports_decimal_number(f_type);
    f_decimal_number = decimal_number;
}

color node::get_color() const
{
    type_supports_color(f_type);

    union color_transfer_t
    {
        uint64_t    f_int;
        double      f_dbl;
        float       f_flt[2];
    };

    color_transfer_t c1, c2;
    c1.f_int = f_integer;
    c2.f_dbl = f_decimal_number;

    color c;
    c.set_color(c1.f_flt[0], c1.f_flt[1], c2.f_flt[0], c2.f_flt[1]);

    return c;
}

void node::set_color(color c)
{
    type_supports_color(f_type);

    union color_transfer_t
    {
        uint64_t    f_int;
        double      f_dbl;
        float       f_flt[2];
    };

    color_transfer_t c1, c2;
    c.get_color(c1.f_flt[0], c1.f_flt[1], c2.f_flt[0], c2.f_flt[1]);

    f_integer        = c1.f_int;
    f_decimal_number = c2.f_dbl;
}

decimal_number_t node::get_font_size() const
{
    type_supports_font_metrics(f_type);

    return f_decimal_number;
}

void node::set_font_size(decimal_number_t font_size)
{
    type_supports_font_metrics(f_type);

    f_decimal_number = font_size;
}

decimal_number_t node::get_line_height() const
{
    type_supports_font_metrics(f_type);

    convert_t c;
    c.f_int = f_integer;
    return c.f_flt;
}

void node::set_line_height(decimal_number_t line_height)
{
    type_supports_font_metrics(f_type);

    convert_t c;
    c.f_flt = line_height;
    f_integer = c.f_int;
}

std::string node::get_dim1() const
{
    type_supports_font_metrics(f_type);

    std::string::size_type pos(f_string.find('/'));
    if(pos == std::string::npos)
    {
        return f_string;
    }
    else
    {
        return f_string.substr(0, pos);
    }
}

void node::set_dim1(std::string const & dimension)
{
    type_supports_font_metrics(f_type);

    if(f_string.empty())
    {
        f_string = dimension;
    }
    else
    {
        std::string::size_type pos(f_string.find('/'));
        if(pos == std::string::npos)
        {
            f_string = dimension;
        }
        else
        {
            f_string = dimension + f_string.substr(pos);
        }
    }
}

std::string node::get_dim2() const
{
    type_supports_font_metrics(f_type);

    std::string::size_type pos(f_string.find('/'));
    if(pos == std::string::npos)
    {
        return "";
    }
    else
    {
        return f_string.substr(pos + 1);
    }
}

void node::set_dim2(std::string const & dimension)
{
    type_supports_font_metrics(f_type);

    if(dimension.empty())
    {
        std::string::size_type pos(f_string.find('/'));
        if(pos != std::string::npos)
        {
            // remove the '/...'
            f_string = f_string.substr(0, pos);
        }
        return;
    }

    if(f_string.empty())
    {
        f_string = "/" + dimension;
    }
    else
    {
        std::string::size_type pos(f_string.find('/'));
        if(pos == std::string::npos)
        {
            f_string += "/" + dimension;
        }
        else
        {
            f_string = f_string.substr(0, pos + 1) + dimension;
        }
    }
}

bool node::empty() const
{
    type_supports_children(f_type);

    return f_children.empty();
}

void node::clear()
{
    type_supports_children(f_type);

    f_children.clear();
}

size_t node::size() const
{
    type_supports_children(f_type);

    return f_children.size();
}

size_t node::child_position(pointer_t child)
{
    type_supports_children(f_type);

    auto it(std::find(f_children.begin(), f_children.end(), child));
    if(it == f_children.end())
    {
        return npos;
    }

    return it - f_children.begin();
}

void node::add_child(pointer_t child)
{
    type_supports_children(f_type);

    // make sure we totally ignore EOF in a child list
    // (this dramatically ease the coding of the parser)
    //
    // also we do not need to save two WHITESPACE tokens
    // one after another
    //
    if(!child->is(node_type_t::EOF_TOKEN)
    && (!child->is(node_type_t::WHITESPACE)
     || f_children.empty()
     || !f_children.back()->is(node_type_t::WHITESPACE)))
    {
        f_children.push_back(child);
    }
}

void node::insert_child(size_t idx, pointer_t child)
{
    // attempting to insert at the end?
    if(idx == f_children.size())
    {
        add_child(child);
        return;
    }

    type_supports_children(f_type);

    if(idx >= f_children.size())
    {
        throw csspp_exception_overflow("insert_child() called with an index out of range.");
    }

    // avoid the EOF_TOKEN, although really it should not happen here
    if(!child->is(node_type_t::EOF_TOKEN))
    {
        f_children.insert(f_children.begin() + idx, child);
    }
}

void node::remove_child(pointer_t child)
{
    type_supports_children(f_type);

    auto it(std::find(f_children.begin(), f_children.end(), child));
    if(it == f_children.end())
    {
        throw csspp_exception_logic("remove_child() called with a node which is not a child of this node.");
    }

    f_children.erase(it);
}

void node::remove_child(size_t idx)
{
    type_supports_children(f_type);

    if(idx >= f_children.size())
    {
        throw csspp_exception_overflow("remove_child() called with an index out of range.");
    }

    f_children.erase(f_children.begin() + idx);
}

node::pointer_t node::get_child(size_t idx) const
{
    type_supports_children(f_type);

    if(idx >= f_children.size())
    {
        throw csspp_exception_overflow("get_child() called with an index out of range.");
    }

    return f_children[idx];
}

node::pointer_t node::get_last_child() const
{
    // if empty, get_child() will throw
    return get_child(f_children.size() - 1);
}

void node::take_over_children_of(pointer_t n)
{
    type_supports_children(f_type);
    type_supports_children(n->f_type);

    // children are copied to this node and cleared
    // in the other node (TBD: should this node have
    // an empty list of children to start with?)
    f_children.clear();
    swap(f_children, n->f_children);
}

void node::replace_child(pointer_t o, pointer_t n)
{
    auto it(std::find(f_children.begin(), f_children.end(), o));
    if(it == f_children.end())
    {
//std::cerr << "------------ Node being replaced:\n" << *o
//          << "------------ Node to replace with:\n" << *n
//          << "------------ This node:\n" << *this
//          << "+++++++++++++++++++++++++++++++++++++++\n";
        throw csspp_exception_logic("replace_child() called with a node which is not a child of this node.");
    }

    size_t const pos(it - f_children.begin());
    f_children.insert(it, n);
    f_children.erase(f_children.begin() + pos + 1);
}

void node::clear_variables()
{
    f_variables.clear();
}

void node::set_variable(std::string const & name, pointer_t value)
{
    f_variables[name] = value;
}

void node::copy_variable(node::pointer_t source)
{
    if(source)
    {
        for(auto v : source->f_variables)
        {
            f_variables[v.first] = v.second->clone();
        }
    }
}

node::pointer_t node::get_variable(std::string const & name)
{
    auto const it(f_variables.find(name));
    if(it == f_variables.end())
    {
        return pointer_t();
    }
    return it->second;
}

void node::clear_flags()
{
    f_flags.clear();
}

void node::set_flag(std::string const & name, bool value)
{
    if(value)
    {
        f_flags[name] = value;
    }
    else
    {
        auto it(f_flags.find(name));
        if(it != f_flags.end())
        {
            f_flags.erase(it);
        }
    }
}

bool node::get_flag(std::string const & name)
{
    auto it(f_flags.find(name));
    return it != f_flags.end();
}

std::string node::to_string(int flags) const
{
    std::stringstream out;

    switch(f_type)
    {
    case node_type_t::ADD:
        out << "+";
        break;

    case node_type_t::AND:
        out << "&&";
        break;

    case node_type_t::ASSIGNMENT:
        out << ":=";
        break;

    case node_type_t::AT_KEYWORD:
        out << "@" << f_string;
        break;

    case node_type_t::BOOLEAN:
        out << (f_boolean ? "true" : "false");
        break;

    case node_type_t::COLON:
        out << ":";
        break;

    case node_type_t::COLOR:
        {
            color c(get_color());
            out << c.to_string();
        }
        break;

    case node_type_t::COLUMN:
        out << "||";
        break;

    case node_type_t::COMMA:
        out << ",";
        break;

    case node_type_t::COMMENT:
        if(f_integer == 0)
        {
            // note: a completely empty comment is possible here and
            // nothing will be output; however, in a valid CSS Preprocessor
            // output, only comments with the @preserve keyword are kept
            // so it won't be empty (until we decide to remove the @preserve
            // from the comments...)
            //
            std::string::size_type start(0);
            std::string::size_type end(f_string.find('\n'));
            while(end != std::string::npos)
            {
                out << "// " << f_string.substr(start, end - start) << std::endl;
                start = end + 1;
                end = f_string.find('\n', start);
            }
            if(start < f_string.size())
            {
                out << "// " << f_string.substr(start) << std::endl;
            }
        }
        else
        {
            out << "/* " << f_string << " */";
        }
        break;

    case node_type_t::CONDITIONAL:
        out << '?';
        break;

    case node_type_t::DASH_MATCH:
        out << "|=";
        break;

    case node_type_t::DECIMAL_NUMBER:
        // this may be a dimension, if not f_string is empty anyway
        out << (f_boolean && f_integer >= 0 ? "+" : "") << f_decimal_number << f_string;
        break;

    case node_type_t::DIVIDE:
        if((flags & g_to_string_flag_add_spaces) != 0)
        {
            out << " / ";
        }
        else
        {
            out << "/";
        }
        break;

    case node_type_t::DOLLAR:
        out << '$';
        break;

    case node_type_t::EQUAL:
        out << '=';
        break;

    case node_type_t::EXCLAMATION:
        out << '!';
        break;

    case node_type_t::FONT_METRICS:
        // this is a mouthful!
        out <<        decimal_number_to_string(get_font_size()   * (get_dim1() == "%" ? 100.0 : 1.0), false) << get_dim1()
            << "/" << decimal_number_to_string(get_line_height() * (get_dim2() == "%" ? 100.0 : 1.0), false) << get_dim2();
        break;

    case node_type_t::VARIABLE_FUNCTION:
        out << '$';
#if __cplusplus >= 201700
        [[fallthrough]];
#endif
    case node_type_t::FUNCTION:
        {
            out << f_string << "(";
            bool first(true);
            for(auto c : f_children)
            {
                if(first)
                {
                    first = false;
                }
                else
                {
                    out << ",";
                }
                out << c->to_string(flags);
            }
            out << ")";
        }
        break;

    case node_type_t::GREATER_EQUAL:
        out << ">=";
        break;

    case node_type_t::GREATER_THAN:
        out << ">";
        break;

    case node_type_t::HASH:
        out << "#" << f_string;
        break;

    case node_type_t::IDENTIFIER:
        out << f_string;
        break;

    case node_type_t::INCLUDE_MATCH:
        out << "~=";
        break;

    case node_type_t::INTEGER:
        // this may be a dimension, if not f_string is empty anyway
        out << (f_boolean && f_integer >= 0 ? "+" : "") << f_integer << f_string;
        break;

    case node_type_t::LESS_EQUAL:
        out << "<=";
        break;

    case node_type_t::LESS_THAN:
        out << "<";
        break;

    case node_type_t::MODULO:
        if((flags & g_to_string_flag_add_spaces) != 0)
        {
            out << " % ";
        }
        else
        {
            out << "%";
        }
        break;

    case node_type_t::MULTIPLY:
        out << "*";
        break;

    case node_type_t::NOT_EQUAL:
        out << "!=";
        break;

    case node_type_t::NULL_TOKEN:
        // should null be "null" or ""?
        out << "";
        break;

    case node_type_t::OPEN_CURLYBRACKET:
        out << "{";
        for(auto c : f_children)
        {
            out << c->to_string(flags);
        }
        out << "}";
        break;

    case node_type_t::OPEN_PARENTHESIS:
        out << "(";
        for(auto c : f_children)
        {
            out << c->to_string(flags);
        }
        out << ")";
        break;

    case node_type_t::OPEN_SQUAREBRACKET:
        out << "[";
        for(auto c : f_children)
        {
            out << c->to_string(flags);
        }
        out << "]";
        break;

    case node_type_t::PERCENT:
        out << (f_boolean && f_integer >= 0 ? "+" : "") << decimal_number_to_string(f_decimal_number * 100.0, false) << "%";
        break;

    case node_type_t::PERIOD:
        out << ".";
        break;

    case node_type_t::PLACEHOLDER:
        out << "%" << f_string;
        break;

    case node_type_t::POWER:
        out << "**";
        break;

    case node_type_t::PRECEDED:
        out << "~";
        break;

    case node_type_t::PREFIX_MATCH:
        out << "^=";
        break;

    case node_type_t::REFERENCE:
        out << "&";
        break;

    case node_type_t::SCOPE:
        out << "|";
        break;

    case node_type_t::SEMICOLON:
        out << ";";
        break;

    case node_type_t::STRING:
        if((flags & g_to_string_flag_show_quotes) != 0)
        {
            int sq(0);
            int dq(0);
            for(char const *s(f_string.c_str()); *s != '\0'; ++s)
            {
                if(*s == '\'')
                {
                    ++sq;
                }
                else if(*s == '"')
                {
                    ++dq;
                }
            }
            if(sq >= dq)
            {
                // use " in this case
                out << '"';
                for(char const *s(f_string.c_str()); *s != '\0'; ++s)
                {
                    if(*s == '"')
                    {
                        out << "\\\"";
                    }
                    else
                    {
                        out << *s;
                    }
                }
                out << '"';
            }
            else
            {
                // use ' in this case
                out << '\'';
                for(char const *s(f_string.c_str()); *s != '\0'; ++s)
                {
                    if(*s == '\'')
                    {
                        out << "\\'";
                    }
                    else
                    {
                        out << *s;
                    }
                }
                out << '\'';
            }
        }
        else
        {
            // for errors and other messages we do not want the quotes
            for(char const *s(f_string.c_str()); *s != '\0'; ++s)
            {
                out << *s;
            }
        }
        break;

    case node_type_t::SUBSTRING_MATCH:
        out << "*=";
        break;

    case node_type_t::SUBTRACT:
        out << "-";
        break;

    case node_type_t::SUFFIX_MATCH:
        out << "$=";
        break;

    case node_type_t::UNICODE_RANGE:
        {
            unicode_range_t const range(static_cast<range_value_t>(f_integer));
            out << "U+" << range.to_string();
        }
        break;

    case node_type_t::URL:
        // TODO: escape special characters or we won't be able to re-read this one
        out << "url(" << f_string << ")";
        break;

    case node_type_t::VARIABLE:
        out << '$' << f_string;
        break;

    case node_type_t::WHITESPACE:
        // this could have been \t or \n...
        out << " ";
        break;

    case node_type_t::COMPONENT_VALUE:
        {
            bool first(true);
            for(auto c : f_children)
            {
                if(c->is(node_type_t::ARG))
                {
                    if(first)
                    {
                        first = false;
                    }
                    else
                    {
                        switch(static_cast<node_type_t>(c->get_integer()))
                        {
                        case node_type_t::UNKNOWN: // this is the default if the integer is never set
                        case node_type_t::COMMA:
                            out << ",";
                            break;

                        case node_type_t::DIVIDE:
                            out << "/";
                            break;

                        default:
                            throw csspp_exception_logic("ARG only supports ',' and '/' as separators.");

                        }
                    }
                    out << c->to_string(flags);
                }
                else
                {
                    // this should not happen unless we did not argify yet
                    // and in that case commas are inline
                    out << c->to_string(flags);
                }
            }
        }
        break;

    case node_type_t::AN_PLUS_B:
        {
            nth_child const an_b(f_integer);
            out << an_b.to_string();
        }
        break;

    case node_type_t::ARG:
        for(auto c : f_children)
        {
            out << c->to_string(flags);
        }
        break;

    case node_type_t::DECLARATION:
        if(!f_string.empty())
        {
            out << f_string << ": ";
        }
        for(size_t idx(0); idx < f_children.size(); ++idx)
        {
            if(f_children[idx]->f_type == node_type_t::ARG)
            {
                out << f_children[idx]->to_string(flags);
                if(idx + 1 != f_children.size())
                {
                    // multiple lists of arguments are comma separated
                    out << ",";
                }
            }
            else
            {
                out << f_children[idx]->to_string(flags);
            }
        }
        break;

    case node_type_t::LIST:
        for(size_t idx(0); idx < f_children.size(); ++idx)
        {
            if(f_children[idx]->f_type == node_type_t::DECLARATION)
            {
                out << f_children[idx]->to_string(flags);
                if(idx + 1 != f_children.size())
                {
                    // multiple declarations are semi-colon separated
                    out << ";";
                }
            }
            else
            {
                out << f_children[idx]->to_string(flags);
            }
        }
        break;

    case node_type_t::ARRAY:
        {
            out << "(";
            bool first(true);
            for(auto c : f_children)
            {
                if(first)
                {
                    first = false;
                }
                else
                {
                    out << ", ";
                }
                out << c->to_string(flags | g_to_string_flag_show_quotes);
            }
            out << ")";
        }
        break;

    case node_type_t::MAP:
        {
            out << "(";
            bool first(true);
            bool label(true);
            for(auto c : f_children)
            {
                if(label)
                {
                    if(first)
                    {
                        first = false;
                    }
                    else
                    {
                        out << ", ";
                    }
                }
                out << c->to_string(flags | g_to_string_flag_show_quotes);
                if(label)
                {
                    out << ": ";
                }
                label = !label;
            }
            out << ")";
        }
        break;

    case node_type_t::FRAME:
        if(f_decimal_number <= 0.0)
        {
            out << "from";
        }
        else if(f_decimal_number >= 1.0)
        {
            out << "to";
        }
        else
        {
            out << decimal_number_to_string(f_decimal_number * 100.0, false) << "%";
        }
        out << "{";
        for(auto c : f_children)
        {
            out << c->to_string(flags);
        }
        out << "}";
        break;

    case node_type_t::UNKNOWN:
    case node_type_t::CDC:
    case node_type_t::CDO:
    case node_type_t::CLOSE_CURLYBRACKET:
    case node_type_t::CLOSE_PARENTHESIS:
    case node_type_t::CLOSE_SQUAREBRACKET:
    case node_type_t::EOF_TOKEN:
    case node_type_t::max_type:
        // many of the nodes are not expected in a valid tree being compiled
        // all of those will generate this exception
        throw csspp_exception_logic("unexpected token in to_string() call.");

    }

    return out.str();
}

void node::display(std::ostream & out, uint32_t indent) const
{
    std::string indent_str;
    for(uint32_t i(0); i < indent; ++i)
    {
        indent_str += " ";
    }
    out << indent_str << f_type;

    switch(f_type)
    {
    case node_type_t::AT_KEYWORD:
    case node_type_t::COMMENT:
    case node_type_t::DECIMAL_NUMBER:
    case node_type_t::DECLARATION:
    case node_type_t::EXCLAMATION:
    case node_type_t::FUNCTION:
    case node_type_t::HASH:
    case node_type_t::IDENTIFIER:
    case node_type_t::INTEGER:
    case node_type_t::PLACEHOLDER:
    case node_type_t::STRING:
    case node_type_t::URL:
    case node_type_t::VARIABLE:
    case node_type_t::VARIABLE_FUNCTION:
        out << " \"" << f_string << "\"";
        break;

    default:
        break;

    }

    switch(f_type)
    {
    case node_type_t::BOOLEAN:
    case node_type_t::OPEN_CURLYBRACKET:
        out << " B:" << (f_boolean ? "true" : "false");
        break;

    default:
        break;

    }

    switch(f_type)
    {
    case node_type_t::AT_KEYWORD:
    case node_type_t::COMMENT:
    case node_type_t::INTEGER:
    case node_type_t::UNICODE_RANGE:
        out << " I:" << f_integer;
        break;

    default:
        break;

    }

    switch(f_type)
    {
    case node_type_t::COLOR:
        {
            color c(get_color());
            out << " H:" << std::hex << c.get_color() << std::dec;
        }
        break;

    default:
        break;

    }

    switch(f_type)
    {
    case node_type_t::FONT_METRICS:
        out << " FM:" << decimal_number_to_string(get_font_size()   * (get_dim1() == "%" ? 100.0 : 1.0), false) << get_dim1()
               << "/" << decimal_number_to_string(get_line_height() * (get_dim2() == "%" ? 100.0 : 1.0), false) << get_dim2();
        break;

    default:
        break;

    }

    switch(f_type)
    {
    case node_type_t::AN_PLUS_B:
        {
            nth_child const an_b(f_integer);
            out << " S:" << an_b.to_string();
        }
        break;

    default:
        break;

    }

    switch(f_type)
    {
    case node_type_t::DECIMAL_NUMBER:
    case node_type_t::PERCENT:
    case node_type_t::FRAME:
        out << " D:" << decimal_number_to_string(f_decimal_number, false);
        break;

    default:
        break;

    }

    for(auto f : f_flags)
    {
        out << " F:" << f.first;
    }

    out << "\n";

    for(auto v : f_variables)
    {
        out << indent_str << "    V:" << v.first << "\n";
        v.second->display(out, indent + 6);
    }


    switch(f_type)
    {
    case node_type_t::ARG:
    case node_type_t::ARRAY:
    case node_type_t::AT_KEYWORD:
    case node_type_t::COMPONENT_VALUE:
    case node_type_t::DECLARATION:
    case node_type_t::EXCLAMATION:
    case node_type_t::FUNCTION:
    case node_type_t::LIST:
    case node_type_t::MAP:
    case node_type_t::OPEN_SQUAREBRACKET:
    case node_type_t::OPEN_CURLYBRACKET:
    case node_type_t::OPEN_PARENTHESIS:
    case node_type_t::VARIABLE_FUNCTION:
    case node_type_t::FRAME:
        // display the children now
        for(size_t i(0); i < f_children.size(); ++i)
        {
            f_children[i]->display(out, indent + 2);
        }
        break;

    default:
        break;

    }
}

void node::limit_nodes_to(uint32_t count)
{
    g_node_max_count = count;
}

} // namespace csspp

std::ostream & operator << (std::ostream & out, csspp::node_type_t const type)
{
    switch(type)
    {
    case csspp::node_type_t::UNKNOWN:
        out << "UNKNOWN";
        break;

    case csspp::node_type_t::ADD:
        out << "ADD";
        break;

    case csspp::node_type_t::AND:
        out << "AND";
        break;

    case csspp::node_type_t::ASSIGNMENT:
        out << "ASSIGNMENT";
        break;

    case csspp::node_type_t::AT_KEYWORD:
        out << "AT_KEYWORD";
        break;

    case csspp::node_type_t::BOOLEAN:
        out << "BOOLEAN";
        break;

    case csspp::node_type_t::CDC:
        out << "CDC";
        break;

    case csspp::node_type_t::CDO:
        out << "CDO";
        break;

    case csspp::node_type_t::CLOSE_CURLYBRACKET:
        out << "CLOSE_CURLYBRACKET";
        break;

    case csspp::node_type_t::CLOSE_PARENTHESIS:
        out << "CLOSE_PARENTHESIS";
        break;

    case csspp::node_type_t::CLOSE_SQUAREBRACKET:
        out << "CLOSE_SQUAREBRACKET";
        break;

    case csspp::node_type_t::COLON:
        out << "COLON";
        break;

    case csspp::node_type_t::COLOR:
        out << "COLOR";
        break;

    case csspp::node_type_t::COLUMN:
        out << "COLUMN";
        break;

    case csspp::node_type_t::COMMA:
        out << "COMMA";
        break;

    case csspp::node_type_t::COMMENT:
        out << "COMMENT";
        break;

    case csspp::node_type_t::CONDITIONAL:
        out << "CONDITIONAL";
        break;

    case csspp::node_type_t::DASH_MATCH:
        out << "DASH_MATCH";
        break;

    case csspp::node_type_t::DECIMAL_NUMBER:
        out << "DECIMAL_NUMBER";
        break;

    case csspp::node_type_t::DIVIDE:
        out << "DIVIDE";
        break;

    case csspp::node_type_t::DOLLAR:
        out << "DOLLAR";
        break;

    case csspp::node_type_t::EOF_TOKEN:
        out << "EOF_TOKEN";
        break;

    case csspp::node_type_t::EQUAL:
        out << "EQUAL";
        break;

    case csspp::node_type_t::EXCLAMATION:
        out << "EXCLAMATION";
        break;

    case csspp::node_type_t::FONT_METRICS:
        out << "FONT_METRICS";
        break;

    case csspp::node_type_t::FUNCTION:
        out << "FUNCTION";
        break;

    case csspp::node_type_t::GREATER_EQUAL:
        out << "GREATER_EQUAL";
        break;

    case csspp::node_type_t::GREATER_THAN:
        out << "GREATER_THAN";
        break;

    case csspp::node_type_t::HASH:
        out << "HASH";
        break;

    case csspp::node_type_t::IDENTIFIER:
        out << "IDENTIFIER";
        break;

    case csspp::node_type_t::INCLUDE_MATCH:
        out << "INCLUDE_MATCH";
        break;

    case csspp::node_type_t::INTEGER:
        out << "INTEGER";
        break;

    case csspp::node_type_t::LESS_EQUAL:
        out << "LESS_EQUAL";
        break;

    case csspp::node_type_t::LESS_THAN:
        out << "LESS_THAN";
        break;

    case csspp::node_type_t::MODULO:
        out << "MODULO";
        break;

    case csspp::node_type_t::MULTIPLY:
        out << "MULTIPLY";
        break;

    case csspp::node_type_t::NOT_EQUAL:
        out << "NOT_EQUAL";
        break;

    case csspp::node_type_t::NULL_TOKEN:
        out << "NULL_TOKEN";
        break;

    case csspp::node_type_t::OPEN_CURLYBRACKET:
        out << "OPEN_CURLYBRACKET";
        break;

    case csspp::node_type_t::OPEN_PARENTHESIS:
        out << "OPEN_PARENTHESIS";
        break;

    case csspp::node_type_t::OPEN_SQUAREBRACKET:
        out << "OPEN_SQUAREBRACKET";
        break;

    case csspp::node_type_t::PERCENT:
        out << "PERCENT";
        break;

    case csspp::node_type_t::PERIOD:
        out << "PERIOD";
        break;

    case csspp::node_type_t::PLACEHOLDER:
        out << "PLACEHOLDER";
        break;

    case csspp::node_type_t::POWER:
        out << "POWER";
        break;

    case csspp::node_type_t::PRECEDED:
        out << "PRECEDED";
        break;

    case csspp::node_type_t::PREFIX_MATCH:
        out << "PREFIX_MATCH";
        break;

    case csspp::node_type_t::REFERENCE:
        out << "REFERENCE";
        break;

    case csspp::node_type_t::SCOPE:
        out << "SCOPE";
        break;

    case csspp::node_type_t::SEMICOLON:
        out << "SEMICOLON";
        break;

    case csspp::node_type_t::STRING:
        out << "STRING";
        break;

    case csspp::node_type_t::SUBSTRING_MATCH:
        out << "SUBSTRING_MATCH";
        break;

    case csspp::node_type_t::SUBTRACT:
        out << "SUBTRACT";
        break;

    case csspp::node_type_t::SUFFIX_MATCH:
        out << "SUFFIX_MATCH";
        break;

    case csspp::node_type_t::UNICODE_RANGE:
        out << "UNICODE_RANGE";
        break;

    case csspp::node_type_t::URL:
        out << "URL";
        break;

    case csspp::node_type_t::VARIABLE:
        out << "VARIABLE";
        break;

    case csspp::node_type_t::VARIABLE_FUNCTION:
        out << "VARIABLE_FUNCTION";
        break;

    case csspp::node_type_t::WHITESPACE:
        out << "WHITESPACE";
        break;

    // Grammar related nodes (i.e. composed nodes)
    case csspp::node_type_t::AN_PLUS_B:
        out << "AN_PLUS_B";
        break;

    case csspp::node_type_t::ARG:
        out << "ARG";
        break;

    case csspp::node_type_t::ARRAY:
        out << "ARRAY";
        break;

    case csspp::node_type_t::COMPONENT_VALUE:
        out << "COMPONENT_VALUE";
        break;

    case csspp::node_type_t::DECLARATION:
        out << "DECLARATION";
        break;

    case csspp::node_type_t::LIST:
        out << "LIST";
        break;

    case csspp::node_type_t::MAP:
        out << "MAP";
        break;

    case csspp::node_type_t::FRAME:
        out << "FRAME";
        break;

    case csspp::node_type_t::max_type:
        out << "max_type";
        break;

    }

    return out;
}

std::ostream & operator << (std::ostream & out, csspp::node const & n)
{
    n.display(out, 0);
    return out;
}

csspp::error & operator << (csspp::error & out, csspp::node_type_t const type)
{
    std::stringstream ss;
    ss << type;
    out << ss.str();
    return out;
}

// Local Variables:
// mode: cpp
// indent-tabs-mode: nil
// c-basic-offset: 4
// tab-width: 4
// End:

// vim: ts=4 sw=4 et
