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
#include    "csspp/expression.h"

namespace csspp
{

class compiler
{
public:
                            compiler(bool validating = false);

    void                    set_root(node::pointer_t root);
    node::pointer_t         get_root() const;

    node::pointer_t         get_result() const;

    void                    set_date_time_variables(time_t now);
    void                    set_empty_on_undefined_variable(bool const empty_on_undefined_variable);
    void                    set_no_logo(bool no_logo = true);

    void                    clear_paths();
    void                    add_path(std::string const & path);

    void                    compile(bool bare);

    std::string             find_file(std::string const & script_name);

private:
    friend class safe_parents_t;
    friend class safe_compiler_state_t;

    typedef std::vector<std::string>                string_vector_t;
    typedef std::map<std::string, node::pointer_t>  validator_script_vector_t;

    class compiler_state_t
        : public expression_variables_interface
    {
    public:
        void                        set_root(node::pointer_t root);
        node::pointer_t             get_root() const;

        void                        clear_paths();
        void                        add_path(std::string const & path);
        void                        set_paths(compiler_state_t const & state);

        void                        push_parent(node::pointer_t parent);
        void                        pop_parent();
        bool                        empty_parents() const;
        node::pointer_t             get_previous_parent() const;
        void                        set_variable(node::pointer_t variable, node::pointer_t value, bool global) const;

        // implement the expression_variables_interface
        virtual node::pointer_t     get_variable(std::string const & variable_name, bool global_only = false) const;
        virtual node::pointer_t     execute_user_function(node::pointer_t func);
        void                        set_empty_on_undefined_variable(bool const empty_on_undefined_variable);
        bool                        get_empty_on_undefined_variable() const;

        std::string                 find_file(std::string const & script_name);

    private:
        string_vector_t             f_paths = string_vector_t();
        bool                        f_empty_on_undefined_variable = false;
        node::pointer_t             f_root = node::pointer_t();
        node_vector_t               f_parents = node_vector_t();
    };

    void                    add_header_and_footer();

    void                    compile(node::pointer_t n);
    void                    compile_component_value(node::pointer_t n);
    void                    compile_qualified_rule(node::pointer_t n);
    void                    compile_declaration(node::pointer_t n);
    void                    compile_declaration_values(node::pointer_t declaration);
    void                    compile_at_keyword(node::pointer_t n);
    node::pointer_t         compile_expression(node::pointer_t n, bool skip_whitespace, bool list_of_expressions);

    void                    replace_import(node::pointer_t parent, node::pointer_t import, size_t & idx);
    void                    replace_at_keyword(node::pointer_t parent, node::pointer_t n, size_t & idx);
    node::pointer_t         at_keyword_expression(node::pointer_t n);
    void                    replace_if(node::pointer_t parent, node::pointer_t n, size_t idx);
    void                    replace_else(node::pointer_t parent, node::pointer_t n, size_t idx);
    void                    handle_mixin(node::pointer_t n);

    void                    mark_selectors(node::pointer_t n);
    void                    remove_empty_rules(node::pointer_t n);
    void                    replace_variables(node::pointer_t n);
    void                    replace_variable(node::pointer_t parent, node::pointer_t n, size_t & idx);
    void                    set_variable(node::pointer_t n);
    void                    replace_variables_in_comment(node::pointer_t n);
    void                    prepare_function_arguments(node::pointer_t var);
    void                    expand_nested_components(node::pointer_t n);
    void                    expand_nested_rules(node::pointer_t parent, node::pointer_t root, node::pointer_t & last, node::pointer_t n);
    void                    expand_nested_declarations(std::string const & name, node::pointer_t parent, node::pointer_t & root, node::pointer_t n);

    bool                    selector_attribute_check(node::pointer_t parent, size_t & parent_pos, node::pointer_t n);
    bool                    selector_simple_term(node::pointer_t n, size_t & pos);
    bool                    selector_term(node::pointer_t n, size_t & pos);
    bool                    selector_list(node::pointer_t n, size_t & pos);
    bool                    parse_selector(node::pointer_t n);

    void                    set_validation_script(std::string const & script_name);
    void                    add_validation_variable(std::string const & variable_name, node::pointer_t value);
    bool                    run_validation(bool check_only);

    compiler_state_t            f_state = compiler_state_t();

    validator_script_vector_t   f_validator_scripts = validator_script_vector_t();  // caching scripts
    node::pointer_t             f_current_validation_script = node::pointer_t();    // last script selected by set_validator_script()
    node::pointer_t             f_return_result = node::pointer_t();
    bool                        f_compiler_validating = false;
    bool                        f_no_logo = false;
};

} // namespace csspp
// vim: ts=4 sw=4 et
