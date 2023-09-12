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
#pragma once

// self
//
#include    "csspp/color.h"
#include    "csspp/error.h"


// C++ lib
//
#include    <map>
#include    <vector>


namespace csspp
{

enum boolean_t
{
    BOOLEAN_INVALID = -1,
    BOOLEAN_FALSE = 0,
    BOOLEAN_TRUE = 1
};

enum class node_type_t
{
    UNKNOWN,

    // basic token
    ADD,                    // for selectors: E + F, F is the next sibling of E
    AND,
    ASSIGNMENT,
    AT_KEYWORD,
    BOOLEAN,
    CDC,
    CDO,
    CLOSE_CURLYBRACKET,
    CLOSE_PARENTHESIS,
    CLOSE_SQUAREBRACKET,
    COLON,                  // for selectors: pseudo-class, E:first-child
    COLOR,                  // in expressions, #RGB or rgb(R,G,B)
    COLUMN,
    COMMA,
    COMMENT,
    CONDITIONAL,
    DASH_MATCH,             // for selectors: dash match E[land|="en"]
    DECIMAL_NUMBER,
    //DIMENSION, -- DECIMAL_NUMBER and INTEGER with a string are dimensions
    DIVIDE,
    DOLLAR,
    EOF_TOKEN,
    EQUAL,                  // for selectors: exact match E[foo="bar"]
    EXCLAMATION,
    FONT_METRICS,           // 12px/14px (font-size/line-height)
    FUNCTION,
    GREATER_EQUAL,
    GREATER_THAN,           // for selectors: E > F, F is a child of E
    HASH,
    IDENTIFIER,
    INCLUDE_MATCH,          // for selectors: include match E[foo~="bar"]
    INTEGER,
    LESS_EQUAL,
    LESS_THAN,
    MODULO,
    MULTIPLY,               // for selectors: '*'
    NOT_EQUAL,
    NULL_TOKEN,
    OPEN_CURLYBRACKET,      // holds the children of '{'
    OPEN_PARENTHESIS,       // holds the children of '('
    OPEN_SQUAREBRACKET,     // holds the children of '['
    PERCENT,
    PERIOD,                 // for selectors: E.name, equivalent to E[class~='name']
    PLACEHOLDER,            // extended selectors: E %name or E%name
    POWER,
    PRECEDED,               // for selectors: E ~ F, F is a sibling after E
    PREFIX_MATCH,           // for selectors: prefix match E[foo^="bar"]
    REFERENCE,
    SCOPE,                  // '|' used in 'ns|E'
    SEMICOLON,
    STRING,
    SUBSTRING_MATCH,        // for selectors: substring match E[foo*="bar"]
    SUBTRACT,
    SUFFIX_MATCH,           // for selectors: suffix match E[foo$="bar"]
    UNICODE_RANGE,
    URL,
    VARIABLE,
    VARIABLE_FUNCTION,
    WHITESPACE,

    // composed tokens
    AN_PLUS_B,              // An+B for nth-child() functions
    ARG,                    // broken up comma separated elements end up in lists of arguments (for functions and qualified rule selectors)
    ARRAY,                  // "value value value ...", like a map, only just indexed with integers
    COMPONENT_VALUE,        // "token token token ..." representing a component-value-list
    DECLARATION,            // <id> ':' ...
    LIST,                   // bare "token token token ..." until better qualified
    MAP,                    // "index value index value ..." (a property list)
    FRAME,                  // @keyframes <name> { frame { ... } frame { ... } ... };

    max_type
};

// useful for quick switchs
int32_t constexpr mix_node_types(node_type_t a, node_type_t b)
{
    return static_cast<int32_t>(a) * 65536 + static_cast<int32_t>(b);
}

// the std::enable_shared_from_this<>() has no virtual dtor
//#pragma GCC diagnostic push
//#pragma GCC diagnostic ignored "-Weffc++"
//#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
class node
    : public std::enable_shared_from_this<node>
{
public:
    typedef std::shared_ptr<node>   pointer_t;
    static size_t const             npos = static_cast<size_t>(-1);

    static int const                g_to_string_flag_show_quotes = 0x01;
    static int const                g_to_string_flag_add_spaces  = 0x02;

                        node(node_type_t const type, position const & pos);
                        ~node();

    pointer_t           clone() const;

    node_type_t         get_type() const;
    bool                is(node_type_t const type) const;
    boolean_t           to_boolean() const;

    position const &    get_position() const;
    std::string const & get_string() const;
    void                set_string(std::string const & str);
    std::string const & get_lowercase_string() const;
    void                set_lowercase_string(std::string const & str);
    integer_t           get_integer() const;
    void                set_integer(integer_t integer);
    bool                get_boolean() const;
    void                set_boolean(bool integer);
    decimal_number_t    get_decimal_number() const;
    void                set_decimal_number(decimal_number_t decimal_number);
    color               get_color() const;
    void                set_color(color c);
    decimal_number_t    get_font_size() const;
    void                set_font_size(decimal_number_t font_size);
    decimal_number_t    get_line_height() const;
    void                set_line_height(decimal_number_t line_height);
    std::string         get_dim1() const;
    void                set_dim1(std::string const & font_size);
    std::string         get_dim2() const;
    void                set_dim2(std::string const & line_height);

    bool                empty() const;
    void                clear();
    size_t              size() const;
    size_t              child_position(pointer_t child);
    void                add_child(pointer_t child);
    void                insert_child(size_t idx, pointer_t child);
    void                remove_child(pointer_t child);
    void                remove_child(size_t idx);
    pointer_t           get_child(size_t idx) const;
    pointer_t           get_last_child() const;
    void                take_over_children_of(pointer_t n);
    void                replace_child(pointer_t o, pointer_t n);

    void                clear_variables();
    void                set_variable(std::string const & name, pointer_t value);
    pointer_t           get_variable(std::string const & name);
    void                copy_variable(node::pointer_t source);

    void                clear_flags();
    void                set_flag(std::string const & name, bool value);
    bool                get_flag(std::string const & name);

    std::string         to_string(int flags) const;
    void                display(std::ostream & out, uint32_t indent) const;

    static void         limit_nodes_to(uint32_t count);

private:
    typedef std::vector<pointer_t>                  list_t;
    typedef std::map<std::string, node::pointer_t>  variable_table_t;
    typedef std::map<std::string, bool>             flag_table_t;

    node_type_t         f_type = node_type_t::UNKNOWN;
    position            f_position;
    bool                f_boolean = false;
    integer_t           f_integer = 0;
    decimal_number_t    f_decimal_number = 0.0;
    std::string         f_string = std::string();
    std::string         f_lowercase_string = std::string();
    list_t              f_children = list_t();
    variable_table_t    f_variables = variable_table_t();
    flag_table_t        f_flags = flag_table_t();
};
//#pragma GCC diagnostic pop

typedef std::vector<node::pointer_t>    node_vector_t;

} // namespace csspp

std::ostream & operator << (std::ostream & out, csspp::node_type_t const type);
std::ostream & operator << (std::ostream & out, csspp::node const & n);

csspp::error & operator << (csspp::error & out, csspp::node_type_t const type);

// vim: ts=4 sw=4 et
