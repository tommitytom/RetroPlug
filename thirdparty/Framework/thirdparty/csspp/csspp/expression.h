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
#include    "csspp/node.h"


namespace csspp
{

class expression_variables_interface
{
public:
    virtual                 ~expression_variables_interface() {}

    virtual node::pointer_t get_variable(std::string const & variable_name, bool global_only = false) const = 0;
    virtual node::pointer_t execute_user_function(node::pointer_t func) = 0;
};

// bare pointer problem
//#pragma GCC diagnostic push
//#pragma GCC diagnostic ignored "-Weffc++"
class expression
{
public:
                        expression(node::pointer_t n);

    void                set_variable_handler(expression_variables_interface * handler);

    void                compile_args(bool divide_font_metrics);
    node::pointer_t     compile();

    static bool         boolean(node::pointer_t n);
    static void         set_unique_id_counter(int counter);
    static int          get_unique_id_counter();

private:
    typedef std::map<std::string, node::pointer_t>  variable_vector_t;
    typedef std::vector<std::string>                dimension_vector_t;

    bool                end_of_nodes();
    void                mark_start();
    node::pointer_t     replace_with_result(node::pointer_t result);
    void                next();

    node::pointer_t     compile_list(node::pointer_t parent);

    node::pointer_t     conditional();

    bool                is_label() const;
    node::pointer_t     expression_list();

    node::pointer_t     assignment();

    node::pointer_t     logical_or();

    node::pointer_t     logical_and();

    bool                is_comparable(node::pointer_t lhs, node::pointer_t rhs);
    bool                is_equal(node::pointer_t lhs, node::pointer_t rhs);
    node::pointer_t     equality();

    bool                is_less_than(node::pointer_t lhs, node::pointer_t rhs);
    node::pointer_t     relational();

    node::pointer_t     additive();

    void                dimensions_to_vectors(position const & pos, std::string const & dimension, dimension_vector_t & dividend, dimension_vector_t & divisor);
    std::string         multiplicative_dimension(position const & pos, std::string const & dim1, node_type_t const op, std::string const & dim2);
    std::string         rebuild_dimension(dimension_vector_t const & dividend, dimension_vector_t const & divisor);
    node::pointer_t     multiply(node_type_t op, node::pointer_t lhs, node::pointer_t rhs);
    node::pointer_t     multiplicative();

    node::pointer_t     apply_power(node::pointer_t lhs, node::pointer_t rhs);
    node::pointer_t     power();

    node::pointer_t     post();

    node::pointer_t     unary();

    // in internal_functions.cpp
    node::pointer_t     internal_function__get_any(node::pointer_t func, size_t argn);
    node::pointer_t     internal_function__get_color(node::pointer_t func, size_t argn, color & col);
    node::pointer_t     internal_function__get_number(node::pointer_t func, size_t argn, decimal_number_t & number);
    node::pointer_t     internal_function__get_number_or_percent(node::pointer_t func, size_t argn, decimal_number_t & number);
    node::pointer_t     internal_function__get_string(node::pointer_t func, size_t argn, std::string & str);
    node::pointer_t     internal_function__get_string_or_identifier(node::pointer_t func, size_t argn, std::string & str);

    node::pointer_t     internal_function__abs(node::pointer_t func);
    node::pointer_t     internal_function__acos(node::pointer_t func);
    node::pointer_t     internal_function__adjust_hue(node::pointer_t func);
    node::pointer_t     internal_function__alpha(node::pointer_t func);
    node::pointer_t     internal_function__asin(node::pointer_t func);
    node::pointer_t     internal_function__atan(node::pointer_t func);
    node::pointer_t     internal_function__blue(node::pointer_t func);
    node::pointer_t     internal_function__ceil(node::pointer_t func);
    node::pointer_t     internal_function__cos(node::pointer_t func);
    node::pointer_t     internal_function__decimal_number(node::pointer_t func);
    node::pointer_t     internal_function__floor(node::pointer_t func);
    node::pointer_t     internal_function__frgb(node::pointer_t func);
    node::pointer_t     internal_function__frgba(node::pointer_t func);
    node::pointer_t     internal_function__function_exists(node::pointer_t func);
    node::pointer_t     internal_function__global_variable_exists(node::pointer_t func);
    node::pointer_t     internal_function__green(node::pointer_t func);
    node::pointer_t     internal_function__hsl(node::pointer_t func);
    node::pointer_t     internal_function__hsla(node::pointer_t func);
    node::pointer_t     internal_function__hue(node::pointer_t func);
    node::pointer_t     internal_function__identifier(node::pointer_t func);
    node::pointer_t     internal_function__if(node::pointer_t func);
    node::pointer_t     internal_function__integer(node::pointer_t func);
    node::pointer_t     internal_function__inspect(node::pointer_t func);
    node::pointer_t     internal_function__lightness(node::pointer_t func);
    node::pointer_t     internal_function__log(node::pointer_t func);
    node::pointer_t     internal_function__max(node::pointer_t func);
    node::pointer_t     internal_function__min(node::pointer_t func);
    node::pointer_t     internal_function__not(node::pointer_t func);
    node::pointer_t     internal_function__percentage(node::pointer_t func);
    node::pointer_t     internal_function__random(node::pointer_t func);
    node::pointer_t     internal_function__red(node::pointer_t func);
    node::pointer_t     internal_function__rgb(node::pointer_t func);
    node::pointer_t     internal_function__rgba(node::pointer_t func);
    node::pointer_t     internal_function__round(node::pointer_t func);
    node::pointer_t     internal_function__saturation(node::pointer_t func);
    node::pointer_t     internal_function__sign(node::pointer_t func);
    node::pointer_t     internal_function__sin(node::pointer_t func);
    node::pointer_t     internal_function__sqrt(node::pointer_t func);
    node::pointer_t     internal_function__string(node::pointer_t func);
    node::pointer_t     internal_function__str_length(node::pointer_t func);
    node::pointer_t     internal_function__tan(node::pointer_t func);
    node::pointer_t     internal_function__type_of(node::pointer_t func);
    node::pointer_t     internal_function__unique_id(node::pointer_t funct);
    node::pointer_t     internal_function__unit(node::pointer_t func);
    node::pointer_t     internal_function__variable_exists(node::pointer_t func);

    node::pointer_t     excecute_function(node::pointer_t func);

    node::pointer_t                     f_node = node::pointer_t();
    size_t                              f_pos = 0;
    size_t                              f_start = static_cast<size_t>(-1);
    node::pointer_t                     f_current = node::pointer_t();
    variable_vector_t                   f_variables = variable_vector_t();
    bool                                f_divide_font_metrics = false;
    expression_variables_interface *    f_variable_handler = nullptr;
};
//#pragma GCC diagnostic pop

} // namespace csspp
// vim: ts=4 sw=4 et
