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
 * \brief Implementation of the CSS Preprocessor compiler.
 *
 * The CSS Preprocessor compiler applies the script rules and transform
 * the tree of nodes so it can be output as standard CSS.
 *
 * \sa \ref compiler_reference
 */

// self
//
#include    "csspp/compiler.h"

#include    "csspp/exception.h"
#include    "csspp/nth_child.h"
#include    "csspp/parser.h"


// C++
//
#include    <cmath>
#include    <filesystem>
#include    <fstream>
#include    <iostream>
#include    <chrono>

namespace chr = std::chrono;


// C
//
//#include    <unistd.h>
#include "csspp/unistd.h"


// last include
//
//#include    <snapdev/poison.h>



namespace csspp
{

namespace
{

integer_t const g_if_or_else_undefined    = 0;
integer_t const g_if_or_else_false_so_far = 1;
integer_t const g_if_or_else_executed     = 2;

} // no name namespace

class safe_parents_t
{
public:
    safe_parents_t(compiler::compiler_state_t & state, node::pointer_t n)
        : f_state(state)
    {
        f_state.push_parent(n);
    }

    ~safe_parents_t()
    {
        f_state.pop_parent();
    }

private:
    compiler::compiler_state_t &     f_state;
};

class safe_compiler_state_t
{
public:
    safe_compiler_state_t(compiler::compiler_state_t & state)
        : f_state(state)
        , f_state_copy(state)
    {
    }

    ~safe_compiler_state_t()
    {
        f_state = f_state_copy;
    }

private:
    compiler::compiler_state_t &    f_state;
    compiler::compiler_state_t      f_state_copy;
};

void compiler::compiler_state_t::set_root(node::pointer_t root)
{
    f_root = root;
    f_parents.clear();
}

node::pointer_t compiler::compiler_state_t::get_root() const
{
    return f_root;
}

void compiler::compiler_state_t::clear_paths()
{
    f_paths.clear();
}

void compiler::compiler_state_t::add_path(std::string const & path)
{
    f_paths.push_back(path);
}

void compiler::compiler_state_t::set_paths(compiler_state_t const & state)
{
    // replace out paths with another set
    f_paths = state.f_paths;
}

void compiler::compiler_state_t::push_parent(node::pointer_t parent)
{
    f_parents.push_back(parent);
}

void compiler::compiler_state_t::pop_parent()
{
    f_parents.pop_back();
}

bool compiler::compiler_state_t::empty_parents() const
{
    return f_parents.empty();
}

node::pointer_t compiler::compiler_state_t::get_previous_parent() const
{
    if(f_parents.size() < 2)
    {
        throw csspp_exception_logic("compiler.cpp:compiler::compiler_state_t::get_current_parent(): no previous parents available."); // LCOV_EXCL_LINE
    }

    // return the parent before last
    return f_parents[f_parents.size() - 2];
}

void compiler::compiler_state_t::set_variable(node::pointer_t name, node::pointer_t value, bool global) const
{
    // the name is used in a map to quickly save/retrieve variables
    // so we save the variable / mixin definitions in a list saved
    // along the value of this variable
    std::string const variable_name(name->get_string());
    node::pointer_t v(new node(node_type_t::LIST, value->get_position()));
    v->add_child(name);
    v->add_child(value);

    if(!global)
    {
        size_t pos(f_parents.size());
        while(pos > 0)
        {
            --pos;
            node::pointer_t s(f_parents[pos]);
            if(s->is(node_type_t::OPEN_CURLYBRACKET)
            && s->get_boolean())
            {
                s->set_variable(variable_name, v);
                return;
            }
        }
    }

    // if nothing else make it a global variable
    f_root->set_variable(variable_name, v);
}

node::pointer_t compiler::compiler_state_t::get_variable(std::string const & variable_name, bool global_only) const
{
    if(!global_only)
    {
        size_t pos(f_parents.size());
        while(pos > 0)
        {
            --pos;
            node::pointer_t s(f_parents[pos]);
            switch(s->get_type())
            {
            case node_type_t::OPEN_CURLYBRACKET:
                if(s->get_boolean())
                {
                    node::pointer_t value(s->get_variable(variable_name));
                    if(value)
                    {
                        return value;
                    }
                }
                break;

            default:
                break;

            }
        }
    }

    return f_root->get_variable(variable_name);
}

node::pointer_t compiler::compiler_state_t::execute_user_function(node::pointer_t func)
{
    // search the parents for the node where the function will be set
    node::pointer_t value(get_variable(func->get_string()));
    if(!value)
    {
        // no function (or variables) with that name found, return the
        // input function as is
        return func;
    }

    // internal validity check
    if(!value->is(node_type_t::LIST)
    || value->size() != 2)
    {
        throw csspp_exception_logic("compiler.cpp:compiler::compiler_state_t::execute_user_function(): all functions must be two sub-values in a LIST, the first item being the variable."); // LCOV_EXCL_LINE
    }

    node::pointer_t var(value->get_child(0));
    node::pointer_t val(value->get_child(1));

    if(!var->is(node_type_t::FUNCTION))
    //&& !var->is(node_type_t::VARIABLE_FUNCTION)) -- TBD
    {
        // found something, but that is not a @mixin function...
        return func;
    }

    // the function was already argified in expression::unary()
    //parser::argify(func);

    // define value of each argument
    node::pointer_t root(new node(node_type_t::LIST, val->get_position()));
    if(!val->is(node_type_t::OPEN_CURLYBRACKET))
    {
        throw csspp_exception_logic("compiler.cpp:compiler::compiler_state_t::execute_user_function(): @mixin function is not defined inside a {}-block."); // LCOV_EXCL_LINE
    }

    // make sure we get a copy of the current global variables
    root->copy_variable(f_root);

    size_t max_val_children(val->size());
    for(size_t j(0); j < max_val_children; ++j)
    {
        root->add_child(val->get_child(j)->clone());
    }

    size_t const max_children(var->size());
    size_t const max_input(func->size());
    for(size_t i(0); i < max_children; ++i)
    {
        node::pointer_t arg(var->get_child(i));
        if(!arg->is(node_type_t::ARG))
        {
            // function declaration is invalid!
            throw csspp_exception_logic("compiler.cpp:compiler::compiler_state_t::execute_user_function(): FUNCTION children are not all ARG nodes."); // LCOV_EXCL_LINE
        }
        if(arg->empty())
        {
            throw csspp_exception_logic("compiler.cpp:compiler::compiler_state_t::execute_user_function(): ARG is empty."); // LCOV_EXCL_LINE
        }
        node::pointer_t arg_name(arg->get_child(0));
        if(!arg_name->is(node_type_t::VARIABLE))
        {
            // this was already erred when we created the variable
            //error::instance() << val->get_position()
            //        << "function declaration requires all parameters to be variables, "
            //        << arg_name->get_type()
            //        << " is not acceptable."
            //        << error_mode_t::ERROR_ERROR;
            return func;
        }
        if(i >= max_input)
        {
            // user did not specify this value, check whether we have
            // an optional value
            if(arg->size() > 1)
            {
                // use default value
                node::pointer_t default_param(arg->clone());
                default_param->remove_child(0);  // remove the variable name
                if(default_param->size() == 1)
                {
                    default_param = default_param->get_child(0);
                }
                else
                {
                    node::pointer_t value_list(new node(node_type_t::LIST, arg->get_position()));
                    value_list->take_over_children_of(default_param);
                    default_param = value_list;
                }
                node::pointer_t param_value(new node(node_type_t::LIST, arg->get_position()));
                param_value->add_child(arg_name);
                param_value->add_child(default_param);
                root->set_variable(arg_name->get_string(), param_value);
            }
            else
            {
                // value is missing
                error::instance() << val->get_position()
                        << "missing function variable named \""
                        << arg_name->get_string()
                        << "\" when calling "
                        << func->get_string()
                        << "();."
                        << error_mode_t::ERROR_ERROR;
                return func;
            }
        }
        else
        {
            // copy user provided value
            node::pointer_t user_param(func->get_child(i));
            if(!user_param->is(node_type_t::ARG))
            {
                throw csspp_exception_logic("compiler.cpp:compiler::replace_variable(): user parameter is not an ARG."); // LCOV_EXCL_LINE
            }
            if(user_param->size() == 1)
            {
                user_param = user_param->get_child(0);
            }
            else
            {
                // is that really correct?
                // we may need a component_value instead...
                node::pointer_t list(new node(node_type_t::LIST, user_param->get_position()));
                list->take_over_children_of(user_param);
                user_param = list;
            }
            node::pointer_t param_value(new node(node_type_t::LIST, user_param->get_position()));
            param_value->add_child(arg_name);
            param_value->add_child(user_param->clone());
            root->set_variable(arg_name->get_string(), param_value);
        }
    }

    compiler c(true);
    c.set_root(root);
    c.f_state.f_paths = f_paths;
    c.f_state.f_empty_on_undefined_variable = f_empty_on_undefined_variable;
    // use 'true' here otherwise it would reload the header/footer each time!
    c.compile(true);

    return c.get_result();
}

void compiler::compiler_state_t::set_empty_on_undefined_variable(bool empty_on_undefined_variable)
{
    f_empty_on_undefined_variable = empty_on_undefined_variable;
}

bool compiler::compiler_state_t::get_empty_on_undefined_variable() const
{
    return f_empty_on_undefined_variable;
}

std::string compiler::compiler_state_t::find_file(std::string const & script_name)
{
    // no name?!
    if(script_name.empty())
    {
        return std::string();
    }

    // an absolute path?
    if(script_name[0] == '/')
    {
        if(access(script_name.c_str(), R_OK) == 0)
        {
            return script_name;
        }
        // absolute does not mean we can find the file
        return std::string();
    }

    // no paths at all???
    if(f_paths.empty())
    {
        // should this be "." here instead of the default?
        f_paths.push_back("/usr/lib/csspp/scripts");
    }

    // check with each path and return the first match
    for(auto it : f_paths)
    {
        std::string const name(it == "" ? script_name : it + "/" + script_name);
        if(std::filesystem::exists(name))
        {
            return name;
        }
    }

    // in case we cannot find a file
    return std::string();
}

compiler::compiler(bool validating)
    : f_compiler_validating(validating)
{

}

void compiler::set_root(node::pointer_t root)
{
    f_state.set_root(root);
}

node::pointer_t compiler::get_root() const
{
    return f_state.get_root();
}

node::pointer_t compiler::get_result() const
{
    return f_return_result;
}

void compiler::set_date_time_variables(time_t now)
{
    // make sure we're ready to setup the date and time
    node::pointer_t root(get_root());
    if(!root)
    {
        throw csspp_exception_logic("compiler.cpp: compiler::set_date_time_variables(): function called too soon, root not set yet.");
    }
    
    chr::zoned_time localTime{ chr::current_zone(), chr::system_clock::now() };
    std::stringstream ss;
    ss << localTime;
    std::string time = ss.str();
    char* buf = time.data();

    // convert date/time in a string
    //struct tm t;
    //localtime_r(&now, &t);
    //char buf[20];
    //strftime(buf, sizeof(buf), "%m/%d/%Y%T", &t);

    // save the result in variables

    // usdate
    csspp::node::pointer_t var(new csspp::node(csspp::node_type_t::VARIABLE, root->get_position()));
    var->set_string("_csspp_usdate");
    csspp::node::pointer_t arg(new csspp::node(csspp::node_type_t::STRING, root->get_position()));
    arg->set_string(std::string(buf, 10));
    f_state.set_variable(var, arg, true);

    // month
    var.reset(new csspp::node(csspp::node_type_t::VARIABLE, root->get_position()));
    var->set_string("_csspp_month");
    arg.reset(new csspp::node(csspp::node_type_t::STRING, root->get_position()));
    arg->set_string(std::string(buf, 2));
    f_state.set_variable(var, arg, true);

    // day
    var.reset(new csspp::node(csspp::node_type_t::VARIABLE, root->get_position()));
    var->set_string("_csspp_day");
    arg.reset(new csspp::node(csspp::node_type_t::STRING, root->get_position()));
    arg->set_string(std::string(buf + 3, 2));
    f_state.set_variable(var, arg, true);

    // year
    var.reset(new csspp::node(csspp::node_type_t::VARIABLE, root->get_position()));
    var->set_string("_csspp_year");
    arg.reset(new csspp::node(csspp::node_type_t::STRING, root->get_position()));
    arg->set_string(std::string(buf + 6, 4));
    f_state.set_variable(var, arg, true);

    // time
    var.reset(new csspp::node(csspp::node_type_t::VARIABLE, root->get_position()));
    var->set_string("_csspp_time");
    arg.reset(new csspp::node(csspp::node_type_t::STRING, root->get_position()));
    arg->set_string(std::string(buf + 10, 8));
    f_state.set_variable(var, arg, true);

    // hour
    var.reset(new csspp::node(csspp::node_type_t::VARIABLE, root->get_position()));
    var->set_string("_csspp_hour");
    arg.reset(new csspp::node(csspp::node_type_t::STRING, root->get_position()));
    arg->set_string(std::string(buf + 10, 2));
    f_state.set_variable(var, arg, true);

    // minute
    var.reset(new csspp::node(csspp::node_type_t::VARIABLE, root->get_position()));
    var->set_string("_csspp_minute");
    arg.reset(new csspp::node(csspp::node_type_t::STRING, root->get_position()));
    arg->set_string(std::string(buf + 13, 2));
    f_state.set_variable(var, arg, true);

    // second
    var.reset(new csspp::node(csspp::node_type_t::VARIABLE, root->get_position()));
    var->set_string("_csspp_second");
    arg.reset(new csspp::node(csspp::node_type_t::STRING, root->get_position()));
    arg->set_string(std::string(buf + 16, 2));
    f_state.set_variable(var, arg, true);
}

void compiler::set_empty_on_undefined_variable(bool empty_on_undefined_variable)
{
    f_state.set_empty_on_undefined_variable(empty_on_undefined_variable);
}

void compiler::set_no_logo(bool no_logo)
{
    f_no_logo = no_logo;
}

void compiler::clear_paths()
{
    f_state.clear_paths();
}

void compiler::add_path(std::string const & path)
{
    f_state.add_path(path);
}

void compiler::compile(bool bare)
{
    if(!f_state.get_root())
    {
        throw csspp_exception_logic("compiler.cpp: compiler::compile(): compile() called without a root node pointer, call set_root() first."); // LCOV_EXCL_LINE
    }

    // before we compile anything we want to transform all the variables
    // with their verbatim contents; otherwise the compiler would be way
    // more complex for nothing...
    //
    // Also for the variables to work properly, we immediately handle
    // the @import and @mixins since both may define additional variables.
    // Similarly, we handle control flow (@if, @else, @include, ...)
    //
//std::cerr << "************* COMPILING:\n" << *f_state.get_root() << "-----------------\n";
    if(!bare)
    {
        add_header_and_footer();
    }

    mark_selectors(f_state.get_root());
    if(!f_state.empty_parents())
    {
        throw csspp_exception_logic("compiler.cpp: the stack of parents must always be empty before mark_selectors() returns."); // LCOV_EXCL_LINE
    }

    replace_variables(f_state.get_root());
    if(!f_state.empty_parents())
    {
        throw csspp_exception_logic("compiler.cpp: the stack of parents must always be empty before replace_variables() returns."); // LCOV_EXCL_LINE
    }

    compile(f_state.get_root());
    if(!f_state.empty_parents())
    {
        throw csspp_exception_logic("compiler.cpp: the stack of parents must always be empty before compile() returns"); // LCOV_EXCL_LINE
    }

    remove_empty_rules(f_state.get_root());
    if(!f_state.empty_parents())
    {
        throw csspp_exception_logic("compiler.cpp: the stack of parents must always be empty before remove_empty_rules() returns"); // LCOV_EXCL_LINE
    }

    expand_nested_components(f_state.get_root());
    if(!f_state.empty_parents())
    {
        throw csspp_exception_logic("compiler.cpp: the stack of parents must always be empty before expand_nested_components() returns"); // LCOV_EXCL_LINE
    }
}

void compiler::add_header_and_footer()
{
    // the header is @import "scripts/init.scss"
    //
    {
        position pos("header.scss");
        node::pointer_t header(new node(node_type_t::AT_KEYWORD, pos));
        header->set_string("import");
        node::pointer_t header_string(new node(node_type_t::STRING, pos));
        header_string->set_string("system/init.scss");
        header->add_child(header_string);
        f_state.get_root()->insert_child(0, header);
    }

    // the footer is @import "script/close.scss"
    //
    {
        position pos("footer.scss");
        node::pointer_t footer(new node(node_type_t::AT_KEYWORD, pos));
        footer->set_string("import");
        node::pointer_t footer_string(new node(node_type_t::STRING, pos));
        footer_string->set_string("system/close.scss");
        footer->add_child(footer_string);
        f_state.get_root()->add_child(footer);
    }

    // the close.scss checks this flag
    //
    {
        position pos("close.scss");
        node::pointer_t no_logo(new node(node_type_t::VARIABLE, pos));
        no_logo->set_string("_csspp_no_logo");
        node::pointer_t value(new node(node_type_t::BOOLEAN, pos));
        value->set_boolean(f_no_logo);
        f_state.set_variable(no_logo, value, true);
    }
}

void compiler::compile(node::pointer_t n)
{
    safe_parents_t safe_parents(f_state, n);

    switch(n->get_type())
    {
    case node_type_t::LIST:
    case node_type_t::FRAME:
        // transparent item, just compile all the children
        {
            size_t idx(0);
            while(idx < n->size() && !f_return_result)
            {
                node::pointer_t child(n->get_child(idx));
                compile(child);

                // the child may replace itself with something else
                // in which case we do not want the ++idx
                if(idx < n->size()
                && n->get_child(idx) == child)
                {
                    ++idx;
                }
            }
            // TODO: remove LIST if it now is empty or has 1 item
        }
        break;

    case node_type_t::DECLARATION:
        // because of lists of compolent values this can happen...
        // we just ignore those since it is already compiled
        //
        // (we get it to happen with @framekeys ... { ... } within the list
        // of declarations at a given position)
        //
        break;

    case node_type_t::COMPONENT_VALUE:
        compile_component_value(n);
        break;

    case node_type_t::AT_KEYWORD:
        compile_at_keyword(n);
        break;

    case node_type_t::COMMENT:
        // passthrough tokens
        break;

    default:
        {
            std::stringstream ss;
            ss << "unexpected token (type: " << n->get_type() << ") in compile().";
            throw csspp_exception_unexpected_token(ss.str());
        }

    }
}

void compiler::compile_component_value(node::pointer_t n)
{
    // already compiled?
    if(n->is(node_type_t::DECLARATION))
    {
        // This is really double ugly, I'll have to look into getting
        // my loops straighten up because having to test such in
        // various places is bad!
        //
        // We may want to find a better way to skip these entries...
        // we replace a COMPONENT_VALUE with a DECLARATION and return
        // and the loops think that the COMPONENT_VALUE was "replaced"
        // by new code that needs to be compiled; only we replaced the
        // entry with already compiled data! The best way may be to have
        // a state with a position that we pass around...
        return;
    }

    // there are quite a few cases to handle here:
    //
    //   $variable ':' '{' ... '}'
    //   <field-prefix> ':' '{' ... '}'
    //   <selector-list> '{' ... '}'
    //   $variable ':' ...
    //   <field-name> ':' ...
    //

    if(n->empty())
    {
        // we have a problem, we should already have had an error
        // somewhere?
        return;     // LCOV_EXCL_LINE
    }

    if(n->get_child(0)->is(node_type_t::COMMENT))
    {
        // XXX: verify that this is the right location to chek this
        //      special case, we may want to do it only in the loop
        //      that also accepts plain comments instead of here
        //      which is a function that can get called from deep
        //      inside...

        // get parent of n, remove n from there, replace it by
        // the comment
        node::pointer_t parent(f_state.get_previous_parent());
        size_t pos(parent->child_position(n));
        parent->remove_child(pos);
        parent->insert_child(pos, n->get_child(0));
        return;
    }

    // was that COMPONENT_VALUE already compiled?
    if(n->get_child(0)->is(node_type_t::ARG))
    {
        // the following fix prevents this from happening so at this time
        // I mark this as a problem; we could just return otherwise
        // (like the case the list is a declaration)
        throw csspp_exception_logic("compiler.cpp: found an ARG as the first child of COMPONENT_VALUE, compile_component_value() called twice on the same object?"); // LCOV_EXCL_LINE
    }

    size_t const max_children(n->size());
    size_t count_cv(0);
    for(size_t idx(0); idx < max_children; ++idx)
    {
        node::pointer_t child(n->get_child(idx));
        if(child->is(node_type_t::COMPONENT_VALUE))
        {
            ++count_cv;
        }
    }
    if(count_cv == max_children)
    {
        // this happens when we add elements from a sub {}-block
        // for example, a verbatim:
        //
        //     @if (true) { foo { a: b; } blah { c: d; } }
        //
        node::pointer_t parent(f_state.get_previous_parent());
        size_t pos(parent->child_position(n));
        parent->remove_child(pos);
        for(size_t idx(0); idx < max_children; ++idx, ++pos)
        {
            parent->insert_child(pos, n->get_child(idx));
        }
        // the caller will call us again with the new list of
        // COMPONENT_VALUE nodes as expected
        return;
    }
    else if(count_cv != 0)
    {
        std::cerr << "Invalid node:\n" << *n;                                                           // LCOV_EXCL_LINE
        throw csspp_exception_logic("compiler.cpp: found a COMPONENT_VALUE with a mix of children.");   // LCOV_EXCL_LINE
    }

    // this may be only temporary (until I fix the parser) but at this
    // point we may get an @-keyword inside a COMPONENT_VALUE
    if(n->get_child(0)->is(node_type_t::AT_KEYWORD))
    {
        {
            safe_parents_t safe_parents(f_state, n->get_child(0));
            compile_at_keyword(n->get_child(0));
        }
        if(n->empty())
        {
            // the @-keyword was removed and now the COMPONENT_VALUE is
            // empty so we can just get rid of it
            f_state.get_previous_parent()->remove_child(n);
        }
        return;
    }

    // $variable ':' '{' ... '}'
    if(parser::is_variable_set(n, true))
    {
        throw csspp_exception_logic("compiler.cpp: somehow a variable definition was found while compiling (1)."); // LCOV_EXCL_LINE
    }

    // <field-prefix> ':' '{' ... '}'
    if(parser::is_nested_declaration(n))
    {
        compile_declaration(n);
        return;
    }

    // <selector-list> '{' ... '}'
    if(n->get_last_child()->is(node_type_t::OPEN_CURLYBRACKET))
    {
        // this is a selector list followed by a block of
        // definitions and sub-blocks
        compile_qualified_rule(n);
        return;
    }

    // $variable ':' ... ';'
    if(parser::is_variable_set(n, false))
    {
        throw csspp_exception_logic("compiler.cpp: somehow a variable definition was found while compiling (1)."); // LCOV_EXCL_LINE
    }

    // <field-name> ':' ...
    compile_declaration(n);
}

void compiler::compile_qualified_rule(node::pointer_t n)
{
    // so here we have a list of selectors, that means we can verify
    // that said list is valid (i.e. binary operators are used properly,
    // only valid operators were used, etc.)

    // any selectors?
    if(n->size() <= 1)
    {
        error::instance() << n->get_position()
                << "a qualified rule without selectors is not valid."
                << error_mode_t::ERROR_ERROR;
        return;
    }

    // compile the selectors using a node parser
    // \ref selectors_rules#grammar
    if(!parse_selector(n))
    {
        // an error occurred, forget this entry and move on
        return;
    }

    // compile the block of contents
    node::pointer_t brackets(n->get_last_child());
    if(brackets->empty())
    {
        // an empty block is perfectly valid, it means the whole rule
        // "exists" but is really useless; in SCSS it could be useful
        // for @extends and %placeholder to have such empty rules
        //error::instance() << n->get_position()
        //        << "the {}-block of a qualified rule is missing."
        //        << error_mode_t::ERROR_ERROR;
        return;
    }

    safe_parents_t safe_parents(f_state, brackets);

    for(size_t b(0); b < brackets->size();)
    {
        node::pointer_t child(brackets->get_child(b));
        safe_parents_t safe_list_parents(f_state, child);
        if(child->is(node_type_t::LIST))
        {
            for(size_t idx(0); idx < child->size();)
            {
                node::pointer_t item(child->get_child(idx));
                safe_parents_t safe_sub_parents(f_state, item);
                compile_component_value(item);
                if(idx < child->size()
                && item == child->get_child(idx))
                {
                    ++idx;
                }
            }
        }
        else if(child->is(node_type_t::COMPONENT_VALUE))
        {
            compile_component_value(child);
        }
        if(b < brackets->size()
        && child == brackets->get_child(b))
        {
            ++b;
        }
    }
}

void compiler::compile_declaration(node::pointer_t n)
{
    // already compiled?
    if(n->is(node_type_t::DECLARATION))
    {
        // We may want to find a better way to skip these entries...
        // we replace a COMPONENT_VALUE with a DECLARATION and return
        // and the loops think that the COMPONENT_VALUE was "replaced"
        // by new code that needs to be compiled; only we replaced the
        // entry with already compiled data! The best way may be to have
        // a state with a position that we pass around...
        return;
    }
    if(n->size() < 2)
    {
        // A "declaration" without the ':' and values reaches here:
        //     font-style; italic;  (notice the ';' instead of ':')
        error::instance() << n->get_position()
                << "somehow a declaration list is missing a field name or ':'."
                << error_mode_t::ERROR_ERROR;
        return;
    }

    // first make sure we have a declaration
    // (i.e. IDENTIFIER WHITESPACE ':' ...)
    //
    node::pointer_t identifier(n->get_child(0));
    if(!identifier->is(node_type_t::IDENTIFIER))
    {
        if((identifier->is(node_type_t::MULTIPLY)
         || identifier->is(node_type_t::PERIOD))
        && n->get_child(1)->is(node_type_t::IDENTIFIER))
        {
            // get rid of the asterisk or period
            // This was an IE 7 and earlier web browser trick to allow
            // various CSS entries only for IE...
            n->remove_child(0);
            identifier = n->get_child(0);
            error::instance() << identifier->get_position()
                    << "the '[*|.|!]<field-name>: ...' syntax is not allowed in csspp, we offer other ways to control field names per browser and do not allow such tricks."
                    << error_mode_t::ERROR_WARNING;
        }
        else if(identifier->is(node_type_t::HASH)
             || identifier->is(node_type_t::EXCLAMATION))
        {
            // the !<id> and #<id> will be converted to a declaration so
            // we do not need to do anything more about it
            //
            // This was an IE 7 and earlier web browser trick to allow
            // various CSS entries only for IE...
            error::instance() << identifier->get_position()
                    << "the '#<field-name>: ...' syntax is not allowed in csspp, we offer other ways to control field names per browser and do not allow such tricks."
                    << error_mode_t::ERROR_WARNING;
        }
        else
        {
            error::instance() << identifier->get_position()
                    << "expected an identifier to start a declaration value; got a: " << identifier->get_type() << " instead."
                    << error_mode_t::ERROR_ERROR;
            return;
        }
    }

    // the WHITESPACE is optional, if present, remove it
    node::pointer_t next(n->get_child(1));
    if(next->is(node_type_t::WHITESPACE))
    {
        n->remove_child(1);
        next = n->get_child(1);
    }

    // now we must have a COLON, also remove that COLON
    if(!next->is(node_type_t::COLON))
    {
        error::instance() << n->get_position()
                << "expected a ':' after the identifier of this declaration value; got a: " << n->get_type() << " instead."
                << error_mode_t::ERROR_ERROR;
        return;
    }
    n->remove_child(1);

    if(n->size() < 2)
    {
        error::instance() << n->get_position()
                << "somehow a declaration list is missing fields, this happens if you used an invalid variable."
                << error_mode_t::ERROR_ERROR;
        return;
    }

    // no need to keep the next whitespace if there is one,
    // plus we often do not expect such at the start of a
    // list like we are about to generate.
    if(n->get_child(1)->is(node_type_t::WHITESPACE))
    {
        n->remove_child(1);
    }

    if(n->size() < 2)
    {
        error::instance() << n->get_position()
                << "somehow a declaration list is missing fields, this happens if you used an invalid variable."
                << error_mode_t::ERROR_ERROR;
        return;
    }

    // create a declaration to replace the identifier
    node::pointer_t declaration(new node(node_type_t::DECLARATION, n->get_position()));
    declaration->set_string(identifier->get_string());

    // copy the following children as the children of the declaration
    // (i.e. identifier is element 0, so we copy elements 1 to n)
    size_t const max_children(n->size());
    for(size_t i(1); i < max_children; ++i)
    {
        // since we are removing the children, we always seemingly
        // copy child 1...
        declaration->add_child(n->get_child(1));
        n->remove_child(1);
    }

    // now replace that identifier by its declaration in the parent
    if(n->is(node_type_t::COMPONENT_VALUE))
    {
        // replace the COMPONENT_VALUE instead of the identifier
        // (this happens when a component value has multiple entries)
        f_state.get_previous_parent()->replace_child(n, declaration);
    }
    else
    {
        throw csspp_exception_logic("compiler.cpp: got a node which was not of type COMPONENT_VALUE to replace with the DECLARATION."); // LCOV_EXCL_LINE
        //n->replace_child(identifier, declaration);
    }
    // now the declaration is part of the stack of parents
    safe_parents_t safe_declaration_parent(f_state, declaration);

    // apply the expression parser on the parameters
    // TODO: test that stuff a lot better, right now it does not look correct...
    bool compile_declaration_items(!declaration->empty() && !declaration->get_child(0)->is(node_type_t::OPEN_CURLYBRACKET));
    if(compile_declaration_items)
    {
        for(size_t i(0); i < declaration->size(); ++i)
        {
            node::pointer_t item(declaration->get_child(i));
            switch(item->get_type())
            {
            //case node_type_t::OPEN_CURLYBRACKET:
            case node_type_t::LIST:
            case node_type_t::COMPONENT_VALUE:
                // This means we have a cascade, a field declaration which has
                // sub-fields (font-<name>, border-<name>, etc.)
                compile_declaration_items = false;
                break;

            default:
                break;

            }
        }
    }

    if(compile_declaration_items
    && !declaration->empty())
    {
        node::pointer_t child(declaration->get_child(0));

        bool const ignore(
                  (child->is(node_type_t::FUNCTION)
                && (child->get_string() == "alpha" || child->get_string() == "chroma" || child->get_string() == "gray" || child->get_string() == "opacity")
                && (declaration->get_string() == "filter" || declaration->get_string() == "-filter"))
            ||
                  (child->is(node_type_t::IDENTIFIER)
                && child->get_string() == "progid"
                && (declaration->get_string() == "filter" || declaration->get_string() == "-filter"))
            );

        if(ignore)
        {
            // Note: the progid does not mean that the function used is
            //       alpha(), but its fairly likely
            //
            // we probably should remove such declarations, but if we want
            // to have functions that output such things, it is important to
            // support this horrible field...
            //
            // alpha() was for IE8 and earlier, now opacity works
            error::instance() << child->get_position()
                    << "the alpha(), chroma() and similar functions of the filter field are Internet Explorer specific extensions which are not supported across browsers."
                    << error_mode_t::ERROR_WARNING;
        }

        if(!ignore)
        {
            // ':' IDENTIFIER

            node::pointer_t declaration_name(new node(node_type_t::STRING, declaration->get_position()));
            declaration_name->set_string(declaration->get_string());

            // check the identifier, if "has-font-metrics" is true, then
            // slashes are viewed as the font metrics separator
            //
            set_validation_script("validation/has-font-metrics");
            add_validation_variable("field_name", declaration_name);
            bool const divide_font_metrics(run_validation(true));

            // if slash-separator returns true then slash (if present)
            // is a separator like a comma in a list of arguments
            set_validation_script("validation/slash-separator");
            add_validation_variable("field_name", declaration_name);
            bool const slash_separators(run_validation(true));

            parser::argify(declaration, slash_separators ? node_type_t::DIVIDE : node_type_t::COMMA);
            expression args_expr(declaration);
            args_expr.set_variable_handler(&f_state);
            args_expr.compile_args(divide_font_metrics);
        }
    }

    compile_declaration_values(declaration);
}

void compiler::compile_declaration_values(node::pointer_t declaration)
{
    // finally compile the parameters of the declaration
    for(size_t i(0); i < declaration->size(); ++i)
    {
        node::pointer_t item(declaration->get_child(i));
        safe_parents_t safe_parents(f_state, item);
        switch(item->get_type())
        {
        case node_type_t::LIST:
            // handle lists recursively
            compile_declaration_values(item);
            break;

        case node_type_t::OPEN_CURLYBRACKET:
            // nested declarations, this {}-block includes sub-field names
            // (i.e. names that will be added this this declaration after a
            // dash (-) and with the name of the fields appearing here)
            for(size_t j(0); j < item->size();)
            {
                node::pointer_t component(item->get_child(j));
                safe_parents_t safe_grand_parents(f_state, component);
                if(component->is(node_type_t::LIST))
                {
                    compile_declaration_values(component);
                }
                else if(component->is(node_type_t::COMPONENT_VALUE))
                {
                    compile_component_value(component);
                }
                else if(component->is(node_type_t::DECLARATION))
                {
                    // this was compiled, ignore
                }
                else
                {
                    // it looks like I cannot get here anymore
                    std::stringstream errmsg;                               // LCOV_EXCL_LINE
                    errmsg << "compiler.cpp: found unexpected node type "   // LCOV_EXCL_LINE
                           << component->get_type()                         // LCOV_EXCL_LINE
                           << ", expected a LIST.";                         // LCOV_EXCL_LINE
                    throw csspp_exception_logic(errmsg.str());              // LCOV_EXCL_LINE
                }
                if(j < item->size()
                && component == item->get_child(j))
                {
                    ++j;
                }
            }
            break;

        case node_type_t::COMPONENT_VALUE:
            compile_component_value(item);
            break;

        default:
            //error::instance() << n->get_position()
            //        << "found a node of type " << item->get_type() << " in a declaration."
            //        << error_mode_t::ERROR_ERROR;
            break;

        }
    }
}

void compiler::compile_at_keyword(node::pointer_t n)
{
    std::string const at(n->get_string());

    node::pointer_t parent(f_state.get_previous_parent());
    node::pointer_t expr(!n->empty() && !n->get_child(0)->is(node_type_t::OPEN_CURLYBRACKET) ? n->get_child(0) : node::pointer_t());

    if(at == "error")
    {
        parent->remove_child(n);

        error::instance() << n->get_position()
                << (expr ? expr->to_string(0) : std::string("@error reached"))
                << error_mode_t::ERROR_ERROR;
        return;
    }

    if(at == "warning")
    {
        parent->remove_child(n);

        error::instance() << n->get_position()
                << (expr ? expr->to_string(0) : std::string("@warning reached"))
                << error_mode_t::ERROR_WARNING;
        return;
    }

    if(at == "info"
    || at == "message")
    {
        parent->remove_child(n);

        error::instance() << n->get_position()
                << (expr ? expr->to_string(0) : std::string("@message reached"))
                << error_mode_t::ERROR_INFO;
        return;
    }

    if(at == "debug")
    {
        parent->remove_child(n);

        error::instance() << n->get_position()
                << (expr ? expr->to_string(0) : std::string("@debug reached"))
                << error_mode_t::ERROR_DEBUG;
        return;
    }

    if(at == "charset")
    {
        // we do not keep the @charset, we always default to UTF-8
        // on all sides (i.e. HTML, XHTML, XML, CSS, even JS...)
        // the assembler could re-introduce such, but again, not required
        parent->remove_child(n);

        if(n->size() != 1
        || !n->get_child(0)->is(node_type_t::STRING))
        {
            error::instance() << n->get_position()
                    << "the @charset is expected to be followed by exactly one string."
                    << error_mode_t::ERROR_ERROR;
            return;
        }

        std::string charset(n->get_child(0)->get_string());
        while(!charset.empty() && std::isspace(charset[0]))
        {
            charset.erase(charset.begin(), charset.begin() + 1);
        }
        while(!charset.empty() && std::isspace(charset.back()))
        {
            charset.erase(charset.end() - 1, charset.end());
        }
        for(auto & c : charset)
        {
            c = std::tolower(c);
        }
        if(charset != "utf-8")
        {
            error::instance() << n->get_position()
                    << "we only support @charset \"utf-8\";, any other encoding is refused."
                    << error_mode_t::ERROR_ERROR;
            return;
        }
        return;
    }

    // TODO: use a validation to determine the list of @-keyword that need
    //       parsing as a qualified rule, a component value, or no parsing
    //       and others are unsupported/unknown (i.e. generate an error)
    if(at == "document"
    || at == "media"
    || at == "supports")
    {
        if(!n->empty())
        {
            node::pointer_t last(n->get_last_child());
            if(last->is(node_type_t::OPEN_CURLYBRACKET))
            {
                if(n->size() > 1)
                {
                    parser::argify(n);
                }
                safe_parents_t safe_list_parents(f_state, last);
                for(size_t idx(0); idx < last->size();)
                {
                    node::pointer_t child(last->get_child(idx));
                    safe_parents_t safe_parents(f_state, child);
                    if(child->is(node_type_t::AT_KEYWORD))
                    {
                        // at this point @-keywords are added inside a
                        // COMPONENT_VALUE so this does not happen...
                        // if we change the parser at some point, this
                        // may happen again so I keep it here
                        compile_at_keyword(child); // LCOV_EXCL_LINE
                    }
                    else
                    {
                        compile_component_value(child);
                    }
                    if(idx < last->size()
                    && child == last->get_child(idx))
                    {
                        ++idx;
                    }
                }
            }
        }
        return;
    }

    if(at == "font-face"
    || at == "page"
    || at == "viewport"
    || at == "-ms-viewport")
    {
        if(!n->empty())
        {
            node::pointer_t last(n->get_last_child());
            if(last->is(node_type_t::OPEN_CURLYBRACKET))
            {
                if(last->size() > 0)
                {
                    node::pointer_t list(last);
                    if(last->get_child(0)->is(node_type_t::LIST))
                    {
                        list = last->get_child(0);
                    }
                    // we may be stacking the same node twice...
                    safe_parents_t safe_grand_parents(f_state, last);
                    safe_parents_t safe_parents(f_state, list);
                    for(size_t idx(0); idx < list->size();)
                    {
                        node::pointer_t child(list->get_child(idx));
                        // TODO: add support for other node types?
                        if(child->is(node_type_t::COMPONENT_VALUE))
                        {
                            safe_parents_t safe_component_parents(f_state, child);
                            compile_component_value(child);
                            if(idx < list->size()
                            && child == list->get_child(idx))
                            {
                                ++idx;
                            }
                        }
                        else
                        {
                            ++idx;
                        }
                    }
                }
            }
        }
        return;
    }

    if(at == "return")
    {
        if(!expr)
        {
            error::instance() << n->get_position()
                    << "@return must be followed by a valid expression."
                    << error_mode_t::ERROR_ERROR;
            return;
        }

        // transform the @return <expr> in a one node result
        //
        expression return_expr(n);
        return_expr.set_variable_handler(&f_state);
        f_return_result = return_expr.compile();
        if(!f_return_result)
        {
            // the expression was erroneous but we cannot return
            // without a valid node otherwise we could end up
            // returning another value "legally"
            //
            // return a NULL as the result
            f_return_result.reset(new node(node_type_t::NULL_TOKEN, n->get_position()));
        }

        return;
    }

    if(at == "keyframes"
    || at == "-o-keyframes"
    || at == "-webkit-keyframes")
    {
        // the format of this @-at keyword is rather peculiar since
        // it expects the identifier "from" or "to" or a percentage
        // followed by a set of components defined between curly
        // brackets
        //
        //     '@keyframes' <name> '{'
        //         'from' | 'to' | <number>'%' '{'
        //             ...  /* regular components */
        //         '}'
        //         ...  /* repeat any number of key frames */
        //     '}';
        //
        // the node tree looks like this:
        //
        //     AT_KEYWORD "keyframes" I:0
        //       IDENTIFIER "progress-bar-stripes"
        //       OPEN_CURLYBRACKET B:true
        //         COMPONENT_VALUE
        //           IDENTIFIER "from"
        //           OPEN_CURLYBRACKET B:true
        //             LIST
        //               COMPONENT_VALUE
        //                 IDENTIFIER "background-position"
        //                 COLON
        //                 WHITESPACE
        //                 INTEGER "px" I:40
        //                 WHITESPACE
        //                 INTEGER "" I:0
        //               COMPONENT_VALUE
        //                 IDENTIFIER "left"
        //                 COLON
        //                 WHITESPACE
        //                 INTEGER "" I:0
        //         COMPONENT_VALUE
        //           PERCENT D:0.3
        //           OPEN_CURLYBRACKET B:true
        //             LIST
        //               COMPONENT_VALUE
        //                 IDENTIFIER "background-position"
        //                 COLON
        //                 WHITESPACE
        //                 INTEGER "px" I:30
        //                 WHITESPACE
        //                 INTEGER "" I:0
        //               COMPONENT_VALUE
        //                 IDENTIFIER "left"
        //                 COLON
        //                 WHITESPACE
        //                 INTEGER "px" I:20
        //         COMPONENT_VALUE
        //           PERCENT D:0.6
        //           OPEN_CURLYBRACKET B:true
        //             LIST
        //               COMPONENT_VALUE
        //                 IDENTIFIER "background-position"
        //                 COLON
        //                 WHITESPACE
        //                 INTEGER "px" I:5
        //                 WHITESPACE
        //                 INTEGER "" I:0
        //               COMPONENT_VALUE
        //                 IDENTIFIER "left"
        //                 COLON
        //                 WHITESPACE
        //                 INTEGER "px" I:27
        //         COMPONENT_VALUE
        //           IDENTIFIER "to"
        //           OPEN_CURLYBRACKET B:true
        //             LIST
        //               COMPONENT_VALUE
        //                 IDENTIFIER "background-position"
        //                 COLON
        //                 WHITESPACE
        //                 INTEGER "" I:0
        //                 WHITESPACE
        //                 INTEGER "" I:0
        //               COMPONENT_VALUE
        //                 IDENTIFIER "left"
        //                 COLON
        //                 WHITESPACE
        //                 INTEGER "px" I:35

//std::cerr << "@keyframes before compiling... [" << *n << "]\n";

        if(n->size() != 2)
        {
            error::instance() << n->get_position()
                    << "@keyframes must be followed by an identifier and '{' ... '}'."
                    << error_mode_t::ERROR_ERROR;
            return;
        }

        // TBD: we may be able to write an expression instead?
        //
        node::pointer_t identifier(n->get_child(0));
        if(!identifier->is(node_type_t::IDENTIFIER))
        {
//std::cerr << "ERROR compiling keyframes (1)\n";
            error::instance() << n->get_position()
                    << "@keyframes must first be followed by an identifier."
                    << error_mode_t::ERROR_ERROR;
            return;
        }

        node::pointer_t positions(n->get_child(1));
        if(!positions->is(node_type_t::OPEN_CURLYBRACKET))
        {
//std::cerr << "ERROR compiling keyframes (2)\n";
            error::instance() << n->get_position()
                    << "@keyframes must be followed by an identifier and '{' ... '}'."
                    << error_mode_t::ERROR_ERROR;
            return;
        }

        // our list of frame positions
        //
        node::pointer_t list(new node(node_type_t::LIST, positions->get_position()));

        size_t const max_positions(positions->size());
        for(size_t idx(0); idx < max_positions; ++idx)
        {
            node::pointer_t component_value(positions->get_child(idx));
            if(!component_value->is(node_type_t::COMPONENT_VALUE))
            {
//std::cerr << "ERROR compiling keyframes (3)\n";
                error::instance() << n->get_position()
                        << "@keyframes is only expecting component values as child entries."
                        << error_mode_t::ERROR_ERROR;
                return;
            }
            if(component_value->size() != 2)
            {
//std::cerr << "ERROR compiling keyframes (4)\n";
                error::instance() << n->get_position()
                        << "@keyframes is expected to be followed by an identifier or a percent number and a '{'."
                        << error_mode_t::ERROR_ERROR;
                return;
            }
            node::pointer_t components(component_value->get_child(1));
            if(!components->is(node_type_t::OPEN_CURLYBRACKET))
            {
//std::cerr << "ERROR compiling keyframes (5)\n";
                error::instance() << n->get_position()
                        << "@keyframes is expected to be followed by an identifier or a percent number and a '{'."
                        << error_mode_t::ERROR_ERROR;
                return;
            }
            if(components->size() != 1)
            {
//std::cerr << "ERROR compiling keyframes (6)\n";
                error::instance() << n->get_position()
                        << "@keyframes is expected to be followed by an identifier or a percent number and a '{'."
                        << error_mode_t::ERROR_ERROR;
                return;
            }
            node::pointer_t component_list(components->get_child(0));
            if(component_list->is(node_type_t::COMPONENT_VALUE))
            {
                // if there is only one component value, there won't be a LIST
                //
                node::pointer_t sub_list(new node(node_type_t::LIST, positions->get_position()));
                sub_list->add_child(component_list);
                component_list = sub_list;
            }
            else if(!component_list->is(node_type_t::LIST))
            {
//std::cerr << "ERROR compiling keyframes (7)\n";
                error::instance() << n->get_position()
                        << "@keyframes is expected to be followed by an identifier or a percent number and a list of component values '{' ... '}'."
                        << error_mode_t::ERROR_ERROR;
                return;
            }

            node::pointer_t position(component_value->get_child(0));

            decimal_number_t p(0.0);
            if(position->is(node_type_t::IDENTIFIER))
            {
                std::string const l(position->get_string());
                if(l == "from")
                {
                    p = 0.0;
                }
                else if(l == "to")
                {
                    p = 1.0;
                }
                else
                {
//std::cerr << "ERROR compiling keyframes (8)\n";
                    error::instance() << n->get_position()
                            << "@keyframes position can be \"from\" or \"to\", other identifiers are not supported."
                            << error_mode_t::ERROR_ERROR;
                    return;
                }
            }
            else if(position->is(node_type_t::PERCENT))
            {
                p = position->get_decimal_number();
                if(p < 0.0 || p > 1.0)
                {
//std::cerr << "ERROR compiling keyframes (9)\n";
                    error::instance() << n->get_position()
                            << "@keyframes position must be a percentage between 0% and 100%."
                            << error_mode_t::ERROR_ERROR;
                    return;
                }
            }
            else
            {
//std::cerr << "ERROR compiling keyframes (10)\n";
                error::instance() << n->get_position()
                        << "@keyframes positions must either be \"from\" or \"to\" or a percent number between 0% and 100%."
                        << error_mode_t::ERROR_ERROR;
                return;
            }

            //compile(component_list);

            // create a frame
            //
            node::pointer_t frame(new node(node_type_t::FRAME, position->get_position()));
            frame->set_decimal_number(p);
            frame->take_over_children_of(component_list);

            list->add_child(frame);

//std::cerr << "frame component list ready? [" << *frame << "]\n";

            // now compile the component list
            //
            compile(frame);
        }

        // first copy the list of frames
        //
        n->take_over_children_of(list);

        // then re-insert the identifier at the start
        //
        n->insert_child(0, identifier);

//std::cerr << "@keyframes in compiler after reorganized: [" << *n << "]\n";

        return;
    }
}

void compiler::replace_import(node::pointer_t parent, node::pointer_t import, size_t & idx)
{
    static_cast<void>(import);

    //
    // WARNING: we do NOT support the SASS extension of multiple entries
    //          within one @import because it is not CSS 2 or CSS 3
    //          compatible, not even remotely
    //

    // node 'import' is the @import itself
    //
    //   @import string | url() [ media-list ] ';'
    //

    node::pointer_t expr(at_keyword_expression(import));

    // we only support arguments with one string
    // (@import accepts strings and url() as their first parameter)
    //
//std::cerr << "replace @import!?\n";
//if(expr) std::cerr << *expr;
//std::cerr << "----------------------\n";
    if(expr
    && (expr->is(node_type_t::STRING)
     || expr->is(node_type_t::URL)))
    {
        std::string script_name(expr->get_string());

        if(expr->is(node_type_t::URL))
        {
            // we only support URIs that start with "file://"
            if(script_name.substr(0, 7) == "file://")
            {
                script_name = script_name.substr(7);
                if(script_name.empty()
                || script_name[0] != '/')
                {
                    script_name = "/" + script_name;
                }
            }
            else
            {
                // not a type of URI we support
                ++idx;
                return;
            }
        }
        else
        {
            // TODO: add code to avoid testing with filenames that represent URIs
            std::string::size_type pos(script_name.find(':'));
            if(pos != std::string::npos
            && script_name.substr(pos, 3) == "://")
            {
                std::string const protocol(script_name.substr(0, pos));
                auto s(protocol.c_str());
                for(; *s != '\0'; ++s)
                {
                    if((*s < 'a' || *s > 'z')
                    && (*s < 'A' || *s > 'Z'))
                    {
                        break;
                    }
                }
                if(*s == '\0')
                {
                    if(protocol != "file")
                    {
                        ++idx;
                        return;
                    }
                    script_name = script_name.substr(7);
                    if(script_name.empty()
                    || script_name[0] != '/')
                    {
                        script_name = "/" + script_name;
                    }
                }
                //else -- not a valid protocol, so we assume it is
                //        a weird filename and use it as is
            }
        }

        // search the corresponding file
        std::string filename(find_file(script_name));
        if(filename.empty() && script_name.length() > 5)
        {
            if(script_name.substr(script_name.size() - 5) != ".scss")
            {
                // try again with the "scss" extension
                filename = find_file(script_name + ".scss");
            }
        }

        // if still not found, we ignore
        if(!filename.empty())
        {
            // found an SCSS include, we remove that @import and replace
            // it (see below) with data as loaded from that file
            //
            // idx will not be incremented as a result
            //
            parent->remove_child(idx);

            // position object for this file
            position pos(filename);

            // TODO: do the necessary to avoid recursive @import

            // we found a file, load it and return it
            std::ifstream in;
            in.open(filename);
            if(!in)
            {
                // the script may not really allow reading even though
                // access() just told us otherwise
                error::instance() << pos                    // LCOV_EXCL_LINE
                        << "validation script \""           // LCOV_EXCL_LINE
                        << script_name                      // LCOV_EXCL_LINE
                        << "\" could not be opened."        // LCOV_EXCL_LINE
                        << error_mode_t::ERROR_ERROR;       // LCOV_EXCL_LINE
            }
            else
            {
                // the file got loaded, parse it and return the root node
                error_happened_t old_count;

                lexer::pointer_t l(new lexer(in, pos));
                parser p(l);
                node::pointer_t list(p.stylesheet());

                if(!old_count.error_happened())
                {
                    // copy valid results at 'idx' which will then be
                    // checked as if it had been part of that script
                    // all along
                    //
                    size_t const max_results(list->size());
                    for(size_t i(0), j(idx); i < max_results; ++i, ++j)
                    {
                        parent->insert_child(j, list->get_child(i));
                    }
                }
            }

            // in this case we managed the entry fully
            return;
        }
        else if(script_name.empty())
        {
            error::instance() << expr->get_position()
                    << "@import \"\"; and @import url(); are not valid."
                    << error_mode_t::ERROR_ERROR;
        }
        else if(expr->is(node_type_t::URL))
        {
            error::instance() << expr->get_position()
                    << "@import uri("
                    << script_name
                    << "); left alone by the CSS Preprocessor, no matching file found."
                    << error_mode_t::ERROR_INFO;
        }
        else
        {
            error::instance() << expr->get_position()
                    << "@import \""
                    << script_name
                    << "\"; left alone by the CSS Preprocessor, no matching file found."
                    << error_mode_t::ERROR_INFO;
        }
    }

    ++idx;
}

void compiler::handle_mixin(node::pointer_t n)
{
    if(n->size() != 2)
    {
        error::instance() << n->get_position()
                << "a @mixin definition expects exactly two parameters: an identifier or function and a {}-block."
                << error_mode_t::ERROR_ERROR;
        return;
    }

    node::pointer_t block(n->get_child(1));
    if(!block->is(node_type_t::OPEN_CURLYBRACKET))
    {
        error::instance() << n->get_position()
                << "a @mixin definition expects a {}-block as its second parameter."
                << error_mode_t::ERROR_ERROR;
        return;
    }

    node::pointer_t name(n->get_child(0));

    // @mixin and @include do not accept $var[()] as a variable name
    // we make the error explicit
    if(name->is(node_type_t::VARIABLE)
    || name->is(node_type_t::VARIABLE_FUNCTION))
    {
        error::instance() << n->get_position()
                << "a @mixin must use an IDENTIFIER or FUNCTION and no a VARIABLE or VARIABLE_FUNCTION."
                << error_mode_t::ERROR_ERROR;
        return;
    }

    // TODO: Are @mixin always global?
    if(name->is(node_type_t::IDENTIFIER))
    {
        // this is just like a variable
        //
        // Note: here we are creating a variable with "name" IDENTIFIER
        // instead of VARIABLE as otherwise expected by the standard
        // variable handling
        //
        f_state.set_variable(name, block, true);
    }
    else if(name->is(node_type_t::FUNCTION))
    {
        // parse the arguments and then save the result
        prepare_function_arguments(name);

        // Note: here we are creating a variable with "name" FUNCTION
        // instead of VARIABLE_FUNCTION as otherwise expected by the
        // standard variable handling
        //
        f_state.set_variable(name, block, true);
    }
    else
    {
        error::instance() << n->get_position()
                << "a @mixin expects either an IDENTIFIER or a FUNCTION as its first parameter."
                << error_mode_t::ERROR_ERROR;
    }
}

void compiler::mark_selectors(node::pointer_t n)
{
    safe_parents_t safe_parents(f_state, n);

    switch(n->get_type())
    {
    case node_type_t::AT_KEYWORD:
    //case node_type_t::ARG:
    case node_type_t::COMPONENT_VALUE:
    case node_type_t::DECLARATION:
    case node_type_t::LIST:
    case node_type_t::OPEN_CURLYBRACKET:
        {
            // there are the few cases we can have here:
            //
            //   $variable ':' '{' ... '}'
            //   <field-prefix> ':' '{' ... '}' <-- this is one we're interested in (nested fields)
            //   <selector-list> '{' ... '}'    <-- this is one we're interested in (regular qualified rule)
            //   $variable ':' ...
            //   <field-name> ':' ...
            //

            if(!n->empty()
            && !parser::is_variable_set(n, true)                        // ! $variable ':' '{' ... '}'
            && n->get_last_child()->is(node_type_t::OPEN_CURLYBRACKET)) // <selector-list> '{' ... '}'
            {
                // this is a selector list followed by a block of
                // definitions and sub-blocks
                n->get_last_child()->set_boolean(true); // accept variables
            }

            // replace all $<var> references with the corresponding value
            for(size_t idx(0); idx < n->size(); ++idx)
            {
                // recursive call to handle all children in the
                // entire tree
                mark_selectors(n->get_child(idx));
            }
        }
        break;

    default:
        // other nodes are not of interest here
        break;

    }
}

void compiler::remove_empty_rules(node::pointer_t n)
{
    safe_parents_t safe_parents(f_state, n);

    switch(n->get_type())
    {
    case node_type_t::COMPONENT_VALUE:
        if(!n->empty()
        && n->get_last_child()->is(node_type_t::OPEN_CURLYBRACKET)
        && n->get_last_child()->empty())
        {
            // that's an empty rule such as:
            //    div {}
            // so get rid of it (i.e. optimization in output, no need for
            // empty rules, really)
            f_state.get_previous_parent()->remove_child(n);
            return;
        }
        [[fallthrough]];
    case node_type_t::AT_KEYWORD:
    //case node_type_t::ARG:
    case node_type_t::DECLARATION:
    case node_type_t::LIST:
    case node_type_t::OPEN_CURLYBRACKET:
        // replace all $<var> references with the corresponding value
        for(size_t idx(0); idx < n->size();)
        {
            // recursive call to handle all children in the
            // entire tree; we need our special handling in
            // case something gets deleted
            node::pointer_t child(n->get_child(idx));
            remove_empty_rules(child);
            if(idx < n->size()
            && child == n->get_child(idx))
            {
                ++idx;
            }
        }
        break;

    default:
        // other nodes are not of interest here
        break;

    }
}

void compiler::replace_variables(node::pointer_t n)
{
    safe_parents_t safe_parents(f_state, n);

    switch(n->get_type())
    {
    case node_type_t::LIST:
        if(n->empty())
        {
            // totally ignore empty lists
            f_state.get_previous_parent()->remove_child(n);
            break;
        }
#if __cplusplus >= 201700
        [[fallthrough]];
#endif
    case node_type_t::AT_KEYWORD:
    case node_type_t::ARG:
    case node_type_t::COMPONENT_VALUE:
    case node_type_t::DECLARATION:
    case node_type_t::FUNCTION:
    case node_type_t::OPEN_CURLYBRACKET:
    case node_type_t::OPEN_PARENTHESIS:
    case node_type_t::OPEN_SQUAREBRACKET:
    case node_type_t::VARIABLE_FUNCTION:
        {
            // handle a special case which SETs a variable and cannot
            // get the first $<var> replaced
            bool is_variable_set(n->get_type() == node_type_t::COMPONENT_VALUE
                              && parser::is_variable_set(n, false));

            // replace all $<var> references with the corresponding value
            size_t idx(is_variable_set
                    ? (n->get_child(0)->is(node_type_t::VARIABLE_FUNCTION)
                        ? n->size() // completely ignore functions
                        : 1)        // do not replace $<var> in $<var>:
                    : 0);           // replace everything
            while(idx < n->size())
            {
                node::pointer_t child(n->get_child(idx));
                if(child->is(node_type_t::VARIABLE))
                {
                    n->remove_child(idx);

                    // search for the variable and replace this 'child' with
                    // the contents of the variable
                    replace_variable(n, child, idx);
                }
                else if(child->is(node_type_t::VARIABLE_FUNCTION))
                {
                    // we need to first replace variables in the parameters
                    // of the function
                    replace_variables(child);

                    n->remove_child(idx);

                    // search for the variable and replace this 'child' with
                    // the contents of the variable
                    replace_variable(n, child, idx);
                }
                else
                {
                    // recursive call to handle all children in the
                    // entire tree
                    switch(child->get_type())
                    {
                    case node_type_t::ARG:
                    case node_type_t::COMMENT:
                    case node_type_t::COMPONENT_VALUE:
                    case node_type_t::DECLARATION:
                    case node_type_t::FUNCTION:
                    case node_type_t::LIST:
                    case node_type_t::OPEN_CURLYBRACKET:
                    case node_type_t::OPEN_PARENTHESIS:
                    case node_type_t::OPEN_SQUAREBRACKET:
                        replace_variables(child);

                        // skip that child if still present
                        if(idx < n->size()
                        && child == n->get_child(idx))
                        {
                            ++idx;
                        }
                        break;

                    case node_type_t::AT_KEYWORD:
                        // handle @import, @mixins, @if, etc.
                        replace_at_keyword(n, child, idx);
                        break;

                    default:
                        ++idx;
                        break;

                    }
                }
            }
            // TODO: remove lists that become empty?

            // handle the special case of a variable assignment
            if(is_variable_set)
            {
                // this is enough to get the variable removed
                // from COMPONENT_VALUE
                set_variable(n);
            }
        }
        break;

    case node_type_t::COMMENT:
        replace_variables_in_comment(n);
        break;

    default:
        // other nodes are not of interest here
        break;

    }
}

void compiler::replace_variable(node::pointer_t parent, node::pointer_t n, size_t & idx)
{
    std::string const & variable_name(n->get_string());

    // search the parents for the node where the variable will be set
    node::pointer_t value(f_state.get_variable(variable_name));
    if(!value)
    {
        // no variable with that name found, generate an error?
        if(!f_state.get_empty_on_undefined_variable())
        {
            error::instance() << n->get_position()
                    << "variable named \""
                    << variable_name
                    << "\" is not set."
                    << error_mode_t::ERROR_ERROR;
        }
        return;
    }

    // internal validity check
    if(!value->is(node_type_t::LIST)
    || value->size() != 2)
    {
        throw csspp_exception_logic("compiler.cpp:compiler::replace_variable(): all variable values must be two sub-values in a LIST, the first item being the variable."); // LCOV_EXCL_LINE
    }

    node::pointer_t var(value->get_child(0));
    node::pointer_t val(value->get_child(1));

    if(var->is(node_type_t::FUNCTION)
    || var->is(node_type_t::VARIABLE_FUNCTION))
    {
        if(!n->is(node_type_t::VARIABLE_FUNCTION)
        && !n->is(node_type_t::FUNCTION))
        {
            error::instance() << n->get_position()
                    << "variable named \""
                    << variable_name
                    << "\" is not a function and it cannot be referenced as such."
                    << error_mode_t::ERROR_ERROR;
            return;
        }
        // we need to apply the function...
        parser::argify(n);

        node::pointer_t root(new node(node_type_t::LIST, val->get_position()));
        root->add_child(val->clone());
        size_t const max_children(var->size());
        size_t const max_input(n->size());
        for(size_t i(0); i < max_children; ++i)
        {
            node::pointer_t arg(var->get_child(i));
            if(!arg->is(node_type_t::ARG))
            {
                // function declaration is invalid!
                throw csspp_exception_logic("compiler.cpp:compiler::replace_variable(): VARIABLE_FUNCTION children are not all ARG nodes."); // LCOV_EXCL_LINE
            }
            if(arg->empty())
            {
                throw csspp_exception_logic("compiler.cpp:compiler::replace_variable(): ARG is empty."); // LCOV_EXCL_LINE
            }
            node::pointer_t arg_name(arg->get_child(0));
            if(!arg_name->is(node_type_t::VARIABLE))
            {
                // this was already erred when we created the variable
                //error::instance() << n->get_position()
                //        << "function declaration requires all parameters to be variables, "
                //        << arg_name->get_type()
                //        << " is not acceptable."
                //        << error_mode_t::ERROR_ERROR;
                return;
            }
            if(i >= max_input)
            {
                // user did not specify this value, check whether we have
                // an optional value
                if(arg->size() > 1)
                {
                    // use default value
                    node::pointer_t default_param(arg->clone());
                    default_param->remove_child(0);  // remove the variable name
                    if(default_param->size() == 1)
                    {
                        default_param = default_param->get_child(0);
                    }
                    else
                    {
                        node::pointer_t value_list(new node(node_type_t::LIST, arg->get_position()));
                        value_list->take_over_children_of(default_param);
                        default_param = value_list;
                    }
                    node::pointer_t param_value(new node(node_type_t::LIST, arg->get_position()));
                    param_value->add_child(arg_name);
                    param_value->add_child(default_param);
                    root->set_variable(arg_name->get_string(), param_value);
                }
                else
                {
                    // value is missing
                    error::instance() << n->get_position()
                            << "missing function variable named \""
                            << arg_name->get_string()
                            << "\" when calling "
                            << variable_name
                            << "() or using @include "
                            << variable_name
                            << "();)."
                            << error_mode_t::ERROR_ERROR;
                    return;
                }
            }
            else
            {
                // copy user provided value
                node::pointer_t user_param(n->get_child(i));
                if(!user_param->is(node_type_t::ARG))
                {
                    throw csspp_exception_logic("compiler.cpp:compiler::replace_variable(): user parameter is not an ARG."); // LCOV_EXCL_LINE
                }
                if(user_param->size() == 1)
                {
                    user_param = user_param->get_child(0);
                }
                else
                {
                    // is that really correct?
                    // we may need a component_value instead...
                    node::pointer_t list(new node(node_type_t::LIST, user_param->get_position()));
                    list->take_over_children_of(user_param);
                    user_param = list;
                }
                node::pointer_t param_value(new node(node_type_t::LIST, user_param->get_position()));
                param_value->add_child(arg_name);
                param_value->add_child(user_param->clone());
                root->set_variable(arg_name->get_string(), param_value);
            }
        }

        compiler c(f_compiler_validating);
        c.set_root(root);
        c.f_state.set_paths(f_state);
        c.f_state.set_empty_on_undefined_variable(f_state.get_empty_on_undefined_variable());
        c.mark_selectors(root);
        c.replace_variables(root);

        // ready to be inserted in the parent
        val = root;

        // only keep the curlybracket instead of list + curlybracket
        if(val->size() == 1
        && val->get_child(0)->is(node_type_t::OPEN_CURLYBRACKET))
        {
            val = val->get_child(0);
        }
    }
    else
    {
        if(n->is(node_type_t::VARIABLE_FUNCTION)
        || n->is(node_type_t::FUNCTION))
        {
            error::instance() << n->get_position()
                    << "variable named \""
                    << variable_name
                    << "\" is a function and it can only be referenced with a function ($"
                    << variable_name
                    << "() or @include "
                    << variable_name
                    << ";)."
                    << error_mode_t::ERROR_ERROR;
            return;
        }
    }

    switch(val->get_type())
    {
    case node_type_t::LIST:
    case node_type_t::OPEN_CURLYBRACKET:
        // check what the content of the list looks like, we may want to
        // insert it as a COMPONENT_VALUE instead of directly as is
        //
        // TODO: the following test is terribly ugly, I'm wondering whether
        //       a "complex" variable should not instead be recompiled in
        //       context; one problem being that we do not really know
        //       what context we're in when we do this transformation...
        //       none-the-less, I think there would be much better ways
        //       to handle the situation.
        //
        if(val->size() == 1
        && val->get_child(0)->is(node_type_t::COMPONENT_VALUE)
        && parent->is(node_type_t::COMPONENT_VALUE))
        {
            size_t const max_children(val->get_child(0)->size());
            for(size_t j(0), i(idx); j < max_children; ++j, ++i)
            {
                node::pointer_t child(val->get_child(0)->get_child(j));
                parent->insert_child(i, child->clone());
            }
            break;
        }
//        else if(val->size() >= 2)
//        {
//std::cerr << "----------------- REPLACE WITH VARIABLE CONTENT:\n" << *val << "----------------------------------\n";
//            bool component_value(false);
//            switch(val->get_child(0)->get_type())
//            {
//            case node_type_t::IDENTIFIER:
//                if(val->get_child(1)->is(node_type_t::WHITESPACE))
//                {
//                    if(val->size() >= 3
//                    && val->get_child(2)->is(node_type_t::COLON))
//                    {
//                        component_value = true;
//                    }
//                }
//                else
//                {
//                    if(val->get_child(1)->is(node_type_t::COLON))
//                    {
//                        component_value = true;
//                    }
//                }
//                if(!component_value)
//                {
//                    component_value = val->get_last_child()->is(node_type_t::OPEN_CURLYBRACKET);
//                }
//                break;
//
//            case node_type_t::MULTIPLY:
//            case node_type_t::OPEN_SQUAREBRACKET:
//            case node_type_t::PERIOD:
//            case node_type_t::REFERENCE:
//            case node_type_t::HASH:
//                component_value = val->get_last_child()->is(node_type_t::OPEN_CURLYBRACKET);
//                break;
//
//            default:
//                // anything else cannot be defining a component value
//                break;
//
//            }
//
//            if(component_value)
//            {
//                // in this case we copy the data in a COMPONENT_VALUE instead
//                // of directly
//                node::pointer_t cv(new node(node_type_t::COMPONENT_VALUE, val->get_position()));
//                parent->insert_child(idx, cv);
//
//                size_t const max_children(val->size());
//                for(size_t j(0); j < max_children; ++j)
//                {
//                    cv->add_child(val->get_child(j)->clone());
//                }
//                break;
//            }
//        }
#if __cplusplus >= 201700
        [[fallthrough]];
#endif
    case node_type_t::OPEN_PARENTHESIS:
    case node_type_t::OPEN_SQUAREBRACKET:
        // in this case we insert the children of 'val'
        // instead of the value itself
        {
            size_t const max_children(val->size());
            for(size_t j(0), i(idx); j < max_children; ++j, ++i)
            {
                parent->insert_child(i, val->get_child(j)->clone());
            }
        }
        break;

    case node_type_t::NULL_TOKEN:
    case node_type_t::WHITESPACE:
        // whitespaces by themselves do not get re-included,
        // which may be a big mistake but at this point
        // it seems wise to do so (plus I don't think it can
        // happen anyway...)
        break;

    default:
        parent->insert_child(idx, val->clone());
        break;

    }
}

void compiler::set_variable(node::pointer_t n)
{
    // WARNING: 'n' is still the COMPONENT_VALUE and not the $var

    // a variable gets removed from the tree and its current value
    // saved in a parent node that is an OPEN_CURLYBRACKET or the
    // root node if no OPEN_CURLYBRACKET is found in the parents
    // (note also that only OPEN_CURLYBRACKET marked with 'true'
    // are used, those are the only valid '{' for variables, for
    // example, an @-keyword '{' does not count...)

    f_state.get_previous_parent()->remove_child(n);

    node::pointer_t var(n->get_child(0));

    n->remove_child(0);     // remove the VARIABLE
    if(n->get_child(0)->is(node_type_t::WHITESPACE))
    {
        n->remove_child(0); // remove the WHITESPACE
    }
    if(!n->get_child(0)->is(node_type_t::COLON))
    {
        throw csspp_exception_logic("compiler.cpp: somehow a variable set is not exactly IDENTIFIER WHITESPACE* ':'."); // LCOV_EXCL_LINE
    }
    n->remove_child(0);     // remove the COLON
    if(!n->empty()
    && n->get_child(0)->is(node_type_t::WHITESPACE))
    {
        // remove WHITESPACE at the beginning of a variable content
        n->remove_child(0);
    }

    // check whether we have a "!global" at the end of the value
    // if so remove it and set global to true
    // similarly, handle the "!default"
    // we should also support the "!important" but that requires a
    // special flag in the variable to know that it cannot be overwritten
    bool global(false);
    bool set_if_unset(false);
    std::string not_at_the_end;
    size_t pos(n->size());
    while(pos > 0)
    {
        --pos;
        node::pointer_t child(n->get_child(pos));
        if(child->is(node_type_t::EXCLAMATION))
        {
            if(child->get_string() == "global")
            {
                global = true;
                n->remove_child(pos);
                if(not_at_the_end.empty()
                && pos != n->size())
                {
                    not_at_the_end = child->get_string();
                }
            }
            else if(child->get_string() == "default")
            {
                set_if_unset = true;
                n->remove_child(pos);
                if(not_at_the_end.empty()
                && pos != n->size())
                {
                    not_at_the_end = child->get_string();
                }
            }
        }
    }
    if(!not_at_the_end.empty())
    {
        error::instance() << n->get_position()
                << "A special flag, !"
                << not_at_the_end
                << " in this case, must only appear at the end of a declaration."
                << error_mode_t::ERROR_WARNING;
    }

    // if variable is already set, return immediately
    if(set_if_unset)
    {
        // TODO: verify that the type (i.e. VARIABLE / VARIABLE_FUNCTION)
        //       would not be changed?
        if(f_state.get_variable(var->get_string()))
        {
            return;
        }
    }

    // rename the node from COMPONENT_VALUE to a plain LIST
    node::pointer_t list;
    if(n->size() == 1)
    {
        list = n->get_child(0);
    }
    else
    {
        list.reset(new node(node_type_t::LIST, n->get_position()));
        list->take_over_children_of(n);
    }

    // we may need to check a little better as null may be in a sub-list
    if(list->is(node_type_t::IDENTIFIER)
    && list->get_string() == "null")
    {
        list.reset(new node(node_type_t::NULL_TOKEN, list->get_position()));
    }

    // now the value of the variable is 'list'; it will get compiled once in
    // context (i.e. not here)

    // search the parents for the node where the variable will be set
    if(var->is(node_type_t::VARIABLE_FUNCTION))
    {
        prepare_function_arguments(var);
    }

    // now save all of that in the best place
    f_state.set_variable(var, list, global);
}

void compiler::prepare_function_arguments(node::pointer_t var)
{
    if(!parser::argify(var))
    {
        return;
    }

    // TODO: verify that the list of arguments is valid (i.e. $var
    // or $var: <default-value>)
    bool optional(false);
    size_t const max_children(var->size());
    for(size_t i(0); i < max_children; ++i)
    {
        node::pointer_t arg(var->get_child(i));
        if(!arg->is(node_type_t::ARG))
        {
            throw csspp_exception_logic("compiler.cpp:compiler::set_variable(): an argument is not an ARG node."); // LCOV_EXCL_LINE
        }
        if(arg->empty())
        {
            throw csspp_exception_logic("compiler.cpp:compiler::set_variable(): an argument has no children."); // LCOV_EXCL_LINE
        }
        node::pointer_t param_var(arg->get_child(0));
        if(param_var->is(node_type_t::VARIABLE))
        {
            size_t const arg_size(arg->size());
            if(arg_size > 1)
            {
                if(arg->get_child(1)->is(node_type_t::WHITESPACE))
                {
                    arg->remove_child(1);
                }
                if(arg->size() > 1
                && arg->get_child(1)->is(node_type_t::COLON))
                {
                    optional = true;
                    arg->remove_child(1);
                    if(arg->size() > 1
                    && arg->get_child(1)->is(node_type_t::WHITESPACE))
                    {
                        arg->remove_child(1);
                    }
                    // so now we have $arg <optional-value> and no whitespaces or colon
                }
                else
                {
                    error::instance() << arg->get_position()
                            << "function declarations expect variable with optional parameters to use a ':' after the variable name and before the optional value."
                            << error_mode_t::ERROR_ERROR;
                }
            }
            else
            {
                // TODO: I think that the last parameter, if it ends with "...", is not required to have an optional value?
                if(optional)
                {
                    error::instance() << arg->get_position()
                            << "function declarations with optional parameters must make all parameters optional from the first one that is given an optional value up to the end of the list of arguments."
                            << error_mode_t::ERROR_ERROR;
                }
            }
        }
        else
        {
            error::instance() << arg->get_position()
                    << "function declarations expect variables for each of their arguments, not a "
                    << param_var->get_type()
                    << "."
                    << error_mode_t::ERROR_ERROR;
        }
    }
}

void compiler::replace_at_keyword(node::pointer_t parent, node::pointer_t n, size_t & idx)
{
    // @<id> [expression] '{' ... '}'
    //
    // Note that the expression is optional. Not only that, in most
    // cases we do not attempt to compile it because it is not expected
    // to be an SCSS expression (especially in an @support command).
    //
    // All the @-keyword that are used to control the flow of the
    // SCSS file are to be handled here; at this time these include:
    //
    //  @else       -- changes what happens (i.e. sets a variable)
    //  @if         -- changes what happens (i.e. sets a variable)
    //  @import     -- changes input code
    //  @include    -- same as $var or $var(args)
    //  @mixin      -- changes variables
    //
    // To be added are: @for, @while, @each.
    //
    std::string const at(n->get_string());

    if(at != "mixin")
    {
        replace_variables(n);
    }

    if(at == "import")
    {
        replace_import(parent, n, idx);
        return;
    }

    if(at == "mixin")
    {
        // mixins are handled like variables or
        // function declarations, so we always
        // remove them
        //
        parent->remove_child(idx);
        handle_mixin(n);
        return;
    }

    if(at == "if")
    {
        // get the position of the @if in its parent so we can insert new
        // data at that position if necessary
        //
        parent->remove_child(idx);
        replace_if(parent, n, idx);
        return;
    }

    if(at == "else")
    {
        // remove the @else from the parent
        parent->remove_child(idx);
        replace_else(parent, n, idx);
        return;
    }

    if(at == "include")
    {
        // this is SASS support, a more explicit way to insert a variable
        // I guess...
        parent->remove_child(idx);

        if(n->empty())
        {
            // as far as I can tell, it is not possible to reach these
            // lines from a tree created by the parser; we could work
            // on creating a "fake" invalid tree too...
            error::instance() << n->get_position()                                                                                  // LCOV_EXCL_LINE
                    << "@include is expected to be followed by an IDENTIFIER or a FUNCTION naming the variable/mixin to include."   // LCOV_EXCL_LINE
                    << error_mode_t::ERROR_ERROR;                                                                                   // LCOV_EXCL_LINE
            return;                                                                                                                 // LCOV_EXCL_LINE
        }

        node::pointer_t id(n->get_child(0));
        if(!id->is(node_type_t::IDENTIFIER)
        && !id->is(node_type_t::FUNCTION))
        {
            error::instance() << n->get_position()
                    << "@include is expected to be followed by an IDENTIFIER or a FUNCTION naming the variable/mixin to include."
                    << error_mode_t::ERROR_ERROR;
            return;
        }

        // search for the variable and replace this 'child' with
        // the contents of the variable
        replace_variable(parent, id, idx);
        return;
    }

    if(at == "error"
    || at == "warning"
    || at == "message"
    || at == "info"
    || at == "debug")
    {
        // make sure the expression is calculated for these
        at_keyword_expression(n);
    }

    // in all other cases the @-keyword is kept as is
    ++idx;
}

node::pointer_t compiler::at_keyword_expression(node::pointer_t n)
{
    // calculate the expression if present
    if(!n->empty() && !n->get_child(0)->is(node_type_t::OPEN_CURLYBRACKET))
    {
        expression expr(n);
        expr.set_variable_handler(&f_state);
        return expr.compile();
    }

    return node::pointer_t();
}

void compiler::replace_if(node::pointer_t parent, node::pointer_t n, size_t idx)
{
    // we want to mark the next block as valid if it is an
    // '@else' or '@else if' and can possibly be inserted
    node::pointer_t next;
    if(idx < parent->size())
    {
        // note: we deleted the @if so 'idx' represents the position of the
        //       next node in the parent array
        next = parent->get_child(idx);
        if(next->is(node_type_t::AT_KEYWORD)
        && next->get_string() == "else")
        {
            // mark that the @else is at the right place
            next->set_integer(g_if_or_else_executed);
        }
    }

    node::pointer_t expr(at_keyword_expression(n));

    // make sure that we got a valid syntax
    if(n->size() != 2 || !expr)
    {
        error::instance() << n->get_position()
                << "@if is expected to have exactly 2 parameters: an expression and a block. This @if has "
                << static_cast<int>(n->size())
                << " parameters."
                << error_mode_t::ERROR_ERROR;
        return;
    }

    bool const r(expression::boolean(expr));
    if(r)
    {
        // BOOLEAN_TRUE, we need the data which we put in the stream
        // at the position of the @if as if the @if and
        // expression never existed
        node::pointer_t block(n->get_child(1));
        size_t const max_children(block->size());
        for(size_t j(0); j < max_children; ++j, ++idx)
        {
            parent->insert_child(idx, block->get_child(j));
        }
    }
    else if(next)
    {
        // mark the else as not executed if r is false
        next->set_integer(g_if_or_else_false_so_far);
    }
}

void compiler::replace_else(node::pointer_t parent, node::pointer_t n, size_t idx)
{
    node::pointer_t next;

    // BOOLEAN_FALSE or BOOLEAN_INVALID, we remove the block to avoid
    // executing it since we do not know whether it should
    // be executed or not; also we mark the next block as
    // "true" if it is an '@else' or '@else if'
    if(idx < parent->size())
    {
        next = parent->get_child(idx);
        if(next->is(node_type_t::AT_KEYWORD)
        && next->get_string() == "else")
        {
            if(n->size() == 1)
            {
                error::instance() << n->get_position()
                        << "'@else { ... }' cannot follow another '@else { ... }'. Maybe you are missing an 'if expr'?"
                        << error_mode_t::ERROR_ERROR;
                return;
            }

            // at this point we do not know the state that the next
            // @else/@else if should have so we use "executed" as a
            // safe value
            //
            next->set_integer(g_if_or_else_executed);
        }
        else
        {
            next.reset();
        }
    }
    node::pointer_t expr;

    bool const else_if(n->get_child(0)->is(node_type_t::IDENTIFIER)
                    && n->get_child(0)->get_string() == "if");

    // in case of an else_if, check the expression
    if(else_if)
    {
        // this is a very special case of the:
        //
        //    @else if expr '{' ... '}'
        //
        // (this is from SASS, if it had been me, I would have used
        // @elseif or @else-if and not @else if ...)
        //
        n->remove_child(0);
        if(!n->empty() && n->get_child(0)->is(node_type_t::WHITESPACE))
        {
            // this should always happen because otherwise we are missing
            // the actual expression!
            n->remove_child(0);
        }
        if(n->size() == 1)
        {
            error::instance() << n->get_position()
                    << "'@else if ...' is missing an expression or a block."
                    << error_mode_t::ERROR_ERROR;
            return;
        }
        expr = at_keyword_expression(n);
    }

    // if this '@else' is still marked with 'g_if_or_else_undefined'
    // then there was no '@if' or '@else if' before it which is an error
    //
    int status(n->get_integer());
    if(status == g_if_or_else_undefined)
    {
        error::instance() << n->get_position()
                << "a standalone @else is not legal, it has to be preceeded by an @if ... or @else if ..."
                << error_mode_t::ERROR_ERROR;
        return;
    }

    //
    // when the '@if' or any '@else if' all had a 'false' expression,
    // we are 'true' here; once one of the '@if' / '@else if' is 'true'
    // then we start with 'r = false'
    //
    bool r(status == g_if_or_else_false_so_far);
    if(n->size() != 1)
    {
        if(n->size() != 2 || !expr)
        {
            error::instance() << n->get_position()
                    << "'@else { ... }' is expected to have 1 parameter, '@else if ... { ... }' is expected to have 2 parameters. This @else has "
                    << static_cast<int>(n->size())
                    << " parameters."
                    << error_mode_t::ERROR_ERROR;
            return;
        }

        // as long as 'status == g_if_or_else_false_so_far' we have
        // not yet found a match (i.e. the starting '@if' was false
        // and any '@else if' were all false so far) so we check the
        // expression of this very '@else if' to know whether to go
        // on or not; r is BOOLEAN_TRUE when the status allows us to check
        // the next expression
        if(r)
        {
            r = expression::boolean(expr);
        }
    }

    if(r)
    {
        status = g_if_or_else_executed;

        // BOOLEAN_TRUE, we need the data which we put in the stream
        // at the position of the @if as if the @if and
        // expression never existed
        node::pointer_t block(n->get_child(n->size() == 1 ? 0 : 1));
        size_t const max_children(block->size());
        for(size_t j(0); j < max_children; ++j, ++idx)
        {
            parent->insert_child(idx, block->get_child(j));
        }
    }

    if(next)
    {
        next->set_integer(status);
    }
}

void compiler::replace_variables_in_comment(node::pointer_t n)
{
    std::string comment(n->get_string());

    std::string::size_type pos(comment.find('{')); // } for vim % functionality
    while(pos != std::string::npos)
    {
        if(pos + 2 < comment.length()
        && comment[pos + 1] == '$')
        {
            std::string::size_type start_var(pos);
            std::string::size_type start_name(start_var + 2);
            // include the preceeding '#' if present (SASS compatibility)
            if(start_var > 0
            && comment[start_var - 1] == '#')
            {
                --start_var;
            }
            // we do +1 because it definitely cannot start at '{$'
            std::string::size_type end_var(comment.find('}', start_var + 2));
            if(end_var != std::string::npos)
            {
                // we got a variable, replace with the content
                std::string const full_name(comment.substr(start_name, end_var - start_name));

                // check whether the user referenced a function or not
                bool is_function(false);
                std::string variable_name(full_name);
                std::string::size_type func_pos(full_name.find('('));
                if(func_pos != std::string::npos)
                {
                    // only keep the name part, ignore the parameters
                    variable_name = full_name.substr(0, func_pos);
                    is_function = true;
                }

                node::pointer_t var_content(f_state.get_variable(variable_name));
                if(var_content)
                {
                    // internal validity check
                    if(!var_content->is(node_type_t::LIST)
                    || var_content->size() != 2)
                    {
                        throw csspp_exception_logic("compiler.cpp:compiler::replace_variables_in_comment(): all variable values must be two sub-values in a LIST, the first item being the variable."); // LCOV_EXCL_LINE
                    }

                    node::pointer_t var(var_content->get_child(0));
                    if(var->is(node_type_t::FUNCTION)
                    || var->is(node_type_t::VARIABLE_FUNCTION))
                    {
                        // TODO: add the support?
                        error::instance() << n->get_position()
                                << "variable named \""
                                << variable_name
                                << "\", is a function which is not supported in a comment."
                                << error_mode_t::ERROR_WARNING;
                    }
                    else
                    {
                        if(is_function)
                        {
                            error::instance() << n->get_position()
                                    << "variable named \""
                                    << variable_name
                                    << "\", is not a function, yet you referenced it as such (and functions are not yet supported in comments)."
                                    << error_mode_t::ERROR_WARNING;
                        }

                        node::pointer_t val(var_content->get_child(1));

                        comment.erase(start_var, end_var + 1 - start_var);
                        // TODO: use the assembler instead of to_string()?
                        std::string const value(val->to_string(0));

                        comment.insert(start_var, value);

                        // adjust pos to continue checking from after the
                        // variable (i.e. if inserting a variable that includes
                        // a {$var} it won't be replaced...)
                        pos = start_var + value.length() - 1;
                    }
                }
                else
                {
                    if(!f_state.get_empty_on_undefined_variable())
                    {
                        error::instance() << n->get_position()
                                << "variable named \""
                                << variable_name
                                << "\", used in a comment, is not set."
                                << error_mode_t::ERROR_WARNING;
                    }
                }
            }
        }

        pos = comment.find('{', pos + 1); // } for vim % functionality
    }

    n->set_string(comment);
}

bool compiler::selector_attribute_check(node::pointer_t parent, size_t & parent_pos, node::pointer_t n)
{
    // use a for() as a 'goto exit;' on a 'break'
    for(;;)
    {
        size_t pos(0);
        node::pointer_t term(n->get_child(pos));
        if(term->is(node_type_t::WHITESPACE))
        {
            // I'm keeping this here, although there should be no WHITESPACE
            // at the start of a '[' block
            n->remove_child(term);          // LCOV_EXCL_LINE
            if(pos >= n->size())            // LCOV_EXCL_LINE
            {
                break;                      // LCOV_EXCL_LINE
            }
            term = n->get_child(pos);       // LCOV_EXCL_LINE
        }

        if(!term->is(node_type_t::IDENTIFIER))
        {
            error::instance() << n->get_position()
                    << "an attribute selector expects to first find an identifier."
                    << error_mode_t::ERROR_ERROR;
            return false;
        }

        ++pos;
        if(pos >= n->size())
        {
            // just IDENTIFIER is valid
            ++parent_pos;
            return true;
        }

        term = n->get_child(pos);
        if(term->is(node_type_t::WHITESPACE))
        {
            n->remove_child(pos);
            if(pos >= n->size())
            {
                // just IDENTIFIER is valid, although we should never
                // reach this line because WHITESPACE are removed from
                // the end of lists
                ++parent_pos;   // LCOV_EXCL_LINE
                return true;    // LCOV_EXCL_LINE
            }
            term = n->get_child(pos);
        }

        if(!term->is(node_type_t::EQUAL)                // '='
        && !term->is(node_type_t::NOT_EQUAL)            // '!=' -- extension
        && !term->is(node_type_t::INCLUDE_MATCH)        // '~='
        && !term->is(node_type_t::PREFIX_MATCH)         // '^='
        && !term->is(node_type_t::SUFFIX_MATCH)         // '$='
        && !term->is(node_type_t::SUBSTRING_MATCH)      // '*='
        && !term->is(node_type_t::DASH_MATCH))          // '|='
        {
            error::instance() << n->get_position()
                    << "expected attribute operator missing, supported operators are '=', '!=', '~=', '^=', '$=', '*=', and '|='."
                    << error_mode_t::ERROR_ERROR;
            return false;
        }
        node::pointer_t op(term);

        ++pos;
        if(pos >= n->size())
        {
            break;
        }

        term = n->get_child(pos);
        if(term->is(node_type_t::WHITESPACE))
        {
            n->remove_child(pos);
            if(pos >= n->size())
            {
                // we actually are not expected to ever have a WHITESPACE
                // at the end of a block so we cannot hit this line, but
                // we keep it, just in case we were wrong...
                break; // LCOV_EXCL_LINE
            }
            term = n->get_child(pos);
        }

        if(!term->is(node_type_t::IDENTIFIER)
        && !term->is(node_type_t::STRING)
        && !term->is(node_type_t::INTEGER)
        && !term->is(node_type_t::DECIMAL_NUMBER))
        {
            error::instance() << n->get_position()
                    << "attribute selector value must be an identifier, a string, an integer, or a decimal number, a "
                    << term->get_type() << " is not acceptable."
                    << error_mode_t::ERROR_ERROR;
            return false;
        }

        ++pos;
        if(pos < n->size())  // <<-- inverted test!
        {
            error::instance() << n->get_position()
                    << "attribute selector cannot be followed by more than one value, found "
                    << n->get_child(pos)->get_type() << " after the value, missing quotes?"
                    << error_mode_t::ERROR_ERROR;
            return false;
        }

        // if the operator was '!=', we have to make changes from:
        //      [a!=b]
        // to
        //      :not([a=b])
        if(op->is(node_type_t::NOT_EQUAL))
        {
            // remove the [a!=b] from parent
            parent->remove_child(parent_pos);

            // add the ':'
            node::pointer_t colon(new node(node_type_t::COLON, n->get_position()));
            parent->insert_child(parent_pos, colon);
            ++parent_pos;

            // add the not()
            node::pointer_t not_func(new node(node_type_t::FUNCTION, n->get_position()));
            not_func->set_string("not");
            parent->insert_child(parent_pos, not_func);

            // in the not() add the [a!=b]
            not_func->add_child(n);

            // remove the '!='
            n->remove_child(1);

            // replace with the '='
            node::pointer_t equal(new node(node_type_t::EQUAL, n->get_position()));
            n->insert_child(1, equal);
        }

        ++parent_pos;

        return true;
    }

    error::instance() << n->get_position()
            << "the attribute selector is expected to be an IDENTIFIER optionally followed by an operator and a value."
            << error_mode_t::ERROR_ERROR;
    return false;
}

bool compiler::selector_simple_term(node::pointer_t n, size_t & pos)
{
    if(pos >= n->size())
    {
        throw csspp_exception_logic("compiler.cpp:compiler::selector_term(): selector_simple_term() called when not enough selectors are available."); // LCOV_EXCL_LINE
    }

    node::pointer_t term(n->get_child(pos));
    switch(term->get_type())
    {
    case node_type_t::HASH:
        // valid term as is
        break;

    case node_type_t::IDENTIFIER:
    case node_type_t::MULTIPLY:
        // IDENTIFIER
        // IDENTIFIER '|' IDENTIFIER
        // IDENTIFIER '|' '*'
        // '*'
        // '*' '|' IDENTIFIER
        // '*' '|' '*'
        if(pos + 1 < n->size())
        {
            if(n->get_child(pos + 1)->is(node_type_t::SCOPE))
            {
                if(pos + 2 >= n->size())
                {
                    error::instance() << n->get_position()
                            << "the scope operator (|) requires a right hand side identifier or '*'."
                            << error_mode_t::ERROR_ERROR;
                    return false;
                }
                pos += 2;
                term = n->get_child(pos);
                if(!term->is(node_type_t::IDENTIFIER)
                && !term->is(node_type_t::MULTIPLY))
                {
                    error::instance() << n->get_position()
                            << "the right hand side of a scope operator (|) must be an identifier or '*'."
                            << error_mode_t::ERROR_ERROR;
                    return false;
                }
            }
            else if(term->is(node_type_t::MULTIPLY)
                 && (n->get_child(pos + 1)->is(node_type_t::OPEN_SQUAREBRACKET)
                  || n->get_child(pos + 1)->is(node_type_t::PERIOD)))
            {
                // this asterisk is not required, get rid of it
                n->remove_child(term);
                return true; // return immediately to avoid the ++pos
            }
        }
        break;

    case node_type_t::SCOPE:
        ++pos;
        if(pos >= n->size())
        {
            error::instance() << n->get_position()
                    << "a scope selector (|) must be followed by an identifier or '*'."
                    << error_mode_t::ERROR_ERROR;
            return false;
        }
        term = n->get_child(pos);
        if(!term->is(node_type_t::IDENTIFIER)
        && !term->is(node_type_t::MULTIPLY))
        {
            error::instance() << n->get_position()
                    << "the right hand side of a scope operator (|) must be an identifier or '*'."
                    << error_mode_t::ERROR_ERROR;
            return false;
        }
        break;

    case node_type_t::COLON:
        ++pos;
        if(pos >= n->size())
        {
            // this is caught by the selector_term() when reading the '::'
            // so we cannot reach this time; keeping just in case though...
            error::instance() << n->get_position()
                    << "a selector list cannot end with a standalone ':'."
                    << error_mode_t::ERROR_ERROR;
            return false;
        }
        term = n->get_child(pos);
        switch(term->get_type())
        {
        case node_type_t::IDENTIFIER:
            {
                // ':' IDENTIFIER
                // validate the identifier as only a small number can be used
                set_validation_script("validation/pseudo-classes");
                node::pointer_t str(new node(node_type_t::STRING, term->get_position()));
                str->set_string(term->get_string());
                add_validation_variable("pseudo_name", str);
                if(!run_validation(false))
                {
                    return false;
                }
            }
            break;

        case node_type_t::FUNCTION:
            {
                // ':' FUNCTION component-value-list ')'
                //
                // create a temporary identifier to run the validation
                // checks, because the FUNCTION is a list of nodes!
                node::pointer_t function_name(new node(node_type_t::STRING, term->get_position()));
                function_name->set_string(term->get_string());
                set_validation_script("validation/pseudo-nth-functions");
                add_validation_variable("pseudo_name", function_name);
                if(run_validation(true))
                {
                    // this is a valid nth function, print out its parameters
                    // and reparse as 'An+B'
                    size_t const max_children(term->size());
                    std::string an_b;
                    for(size_t idx(0); idx < max_children; ++idx)
                    {
                        an_b += term->get_child(idx)->to_string(node::g_to_string_flag_show_quotes);
                    }
                    // TODO...
                    nth_child nc;
                    if(nc.parse(an_b))
                    {
                        // success, save the compiled An+B in this object
                        node::pointer_t an_b_node(new node(node_type_t::AN_PLUS_B, term->get_position()));
                        an_b_node->set_integer(nc.get_nth());
                        term->clear();
                        term->add_child(an_b_node);
                    }
                    else
                    {
                        // get the error and display it
                        error::instance() << term->get_position()
                                << nc.get_error()
                                << error_mode_t::ERROR_ERROR;
                        return false;
                    }
                }
                else
                {
                    set_validation_script("validation/pseudo-functions");
                    add_validation_variable("pseudo_name", function_name);
                    if(!run_validation(false))
                    {
                        return false;
                    }
                    // this is a standard function, check the parameters
                    if(term->get_string() == "not")
                    {
                        // :not(:not(...)) is illegal
                        error::instance() << n->get_position()
                                << "the :not() selector does not accept an inner :not()."
                                << error_mode_t::ERROR_ERROR;
                        return false;
                    }
                    else if(term->get_string() == "lang")
                    {
                        // the language must be an identifier with no dashes
                        if(term->size() != 1)
                        {
                            error::instance() << term->get_position()
                                    << "a lang() function selector must have exactly one identifier as its parameter."
                                    << error_mode_t::ERROR_ERROR;
                            return false;
                        }
                        term = term->get_child(0);
                        if(term->is(node_type_t::IDENTIFIER))
                        {
                            std::string lang(term->get_string());
                            std::string country;
                            std::string::size_type char_pos(lang.find('-'));
                            if(char_pos != std::string::npos)
                            {
                                country = lang.substr(char_pos + 1);
                                lang = lang.substr(0, char_pos);
                                char_pos = country.find('-');
                                if(char_pos != std::string::npos)
                                {
                                    // remove whatever other information that
                                    // we will ignore in our validations
                                    country = country.substr(0, char_pos);
                                }
                            }
                            // check the language (mandatory)
                            node::pointer_t language_name(new node(node_type_t::STRING, term->get_position()));
                            language_name->set_string(lang);
                            set_validation_script("validation/languages");
                            add_validation_variable("language_name", language_name);
                            if(!run_validation(false))
                            {
                                return false;
                            }
                            if(!country.empty())
                            {
                                // check the country (optional)
                                node::pointer_t country_name(new node(node_type_t::STRING, term->get_position()));
                                country_name->set_string(country);
                                set_validation_script("validation/countries");
                                add_validation_variable("country_name", country_name);
                                if(!run_validation(false))
                                {
                                    return false;
                                }
                            }
                        }
                        else
                        {
                            error::instance() << term->get_position()
                                    << "a lang() function selector expects an identifier as its parameter."
                                    << error_mode_t::ERROR_ERROR;
                            return false;
                        }
                    }
                }
            }
            break;

        default:
            // invalid selector list
            error::instance() << n->get_position()
                    << "a ':' selector must be followed by an identifier or a function, a " << n->get_type() << " was found instead."
                    << error_mode_t::ERROR_ERROR;
            return false;

        }
        break;

    case node_type_t::PERIOD:
        // '.' IDENTIFIER -- class (special attribute check)
        ++pos;
        if(pos >= n->size())
        {
            error::instance() << n->get_position()
                    << "a selector list cannot end with a standalone '.'."
                    << error_mode_t::ERROR_ERROR;
            return false;
        }
        term = n->get_child(pos);
        if(!term->is(node_type_t::IDENTIFIER))
        {
            error::instance() << n->get_position()
                    << "a class selector (after a period: '.') must be an identifier."
                    << error_mode_t::ERROR_ERROR;
            return false;
        }
        break;

    case node_type_t::OPEN_SQUAREBRACKET:
        // '[' WHITESPACE attribute-check WHITESPACE ']' -- attributes check
        return selector_attribute_check(n, pos, term);

    case node_type_t::GREATER_THAN:
    case node_type_t::ADD:
    case node_type_t::PRECEDED:
        error::instance() << n->get_position()
                << "found token " << term->get_type() << ", which cannot be used to start a selector expression."
                << error_mode_t::ERROR_ERROR;
        return false;

    case node_type_t::FUNCTION:
        error::instance() << n->get_position()
                << "found function \"" << term->get_string() << "()\", which may be a valid selector token but only if immediately preceeded by one ':' (simple term)."
                << error_mode_t::ERROR_ERROR;
        return false;

    default:
        error::instance() << n->get_position()
                << "found token " << term->get_type() << ", which is not a valid selector token (simple term)."
                << error_mode_t::ERROR_ERROR;
        return false;

    }

    // move on to the next term
    ++pos;

    return true;
}

bool compiler::selector_term(node::pointer_t n, size_t & pos)
{
    if(pos >= n->size())
    {
        throw csspp_exception_logic("compiler.cpp:compiler::selector_term(): selector_term() called when not enough selectors are available."); // LCOV_EXCL_LINE
    }

    node::pointer_t term(n->get_child(pos));
    switch(term->get_type())
    {
    case node_type_t::PLACEHOLDER:
        // valid complex term as is
        break;

    case node_type_t::REFERENCE:
        // valid complex term only if pos == 0
        if(pos != 0)
        {
            error::instance() << n->get_position()
                    << "a selector reference (&) can only appear as the very first item in a list of selectors."
                    << error_mode_t::ERROR_ERROR;
            return false;
        }
        break;

    case node_type_t::COLON:
        // ':' FUNCTION (="not") is a term and has to be managed here
        // '::' IDENTIFIER is a term and not a simple term (it cannot
        //                 appear inside a :not() function.)
        ++pos;
        if(pos >= n->size())
        {
            error::instance() << n->get_position()
                    << "a selector list cannot end with a standalone ':'."
                    << error_mode_t::ERROR_ERROR;
            return false;
        }
        term = n->get_child(pos);
        switch(term->get_type())
        {
        case node_type_t::IDENTIFIER:
            --pos;
            return selector_simple_term(n, pos);

        case node_type_t::FUNCTION:
            // ':' FUNCTION component-value-list ')'
            if(term->get_string() == "not")
            {
                // special handling, the :not() is considered to be
                // a complex selector and as such has to be handled
                // right here; the parameters must represent one valid
                // simple term
                //
                // TODO: still got to take care of WHITESPACE?
                size_t sub_pos(0);
                if(!selector_simple_term(term, sub_pos))
                {
                    return false;
                }
                if(sub_pos < term->size())
                {
                    // we did not reach the end of that list so something
                    // is wrong (i.e. the :not() can only include one
                    // element)
                    error::instance() << term->get_position()
                            << "the :not() function accepts at most one simple term."
                            << error_mode_t::ERROR_ERROR;
                    return false;
                }
            }
            else
            {
                --pos;
                return selector_simple_term(n, pos);
            }
            break;

        case node_type_t::COLON:
            {
                // '::' IDENTIFIER -- pseudo elements
                ++pos;
                if(pos >= n->size())
                {
                    error::instance() << n->get_position()
                            << "a selector list cannot end with a '::' without an identifier after it."
                            << error_mode_t::ERROR_ERROR;
                    return false;
                }
                term = n->get_child(pos);
                if(!term->is(node_type_t::IDENTIFIER))
                {
                    error::instance() << n->get_position()
                            << "a pseudo element name (defined after a '::' in a list of selectors) must be defined using an identifier."
                            << error_mode_t::ERROR_ERROR;
                    return false;
                }
                // only a few pseudo element names exist, do a validation
                node::pointer_t pseudo_element(new node(node_type_t::STRING, term->get_position()));
                pseudo_element->set_string(term->get_string());
                set_validation_script("validation/pseudo-elements");
                add_validation_variable("pseudo_name", pseudo_element);
                if(!run_validation(false))
                {
                    return false;
                }
                if(pos + 1 < n->size())
                {
                    error::instance() << n->get_position()
                            << "a pseudo element name (defined after a '::' in a list of selectors) must be defined as the last element in the list of selectors."
                            << error_mode_t::ERROR_ERROR;
                    return false;
                }
            }
            break;

        default:
            // invalid selector list
            error::instance() << n->get_position()
                    << "a ':' selector must be followed by an identifier or a function, a " << term->get_type() << " was found instead."
                    << error_mode_t::ERROR_ERROR;
            return false;

        }
        break;

    case node_type_t::HASH:
    case node_type_t::IDENTIFIER:
    case node_type_t::MULTIPLY:
    case node_type_t::OPEN_SQUAREBRACKET:
    case node_type_t::PERIOD:
    case node_type_t::SCOPE:
        return selector_simple_term(n, pos);

    case node_type_t::GREATER_THAN:
    case node_type_t::ADD:
    case node_type_t::PRECEDED:
        error::instance() << n->get_position()
                << "found token " << term->get_type() << ", which cannot be used to start a selector expression."
                << error_mode_t::ERROR_ERROR;
        return false;

    case node_type_t::FUNCTION:
        // we can reach this case if we have a token in the selector list
        // which immediately returns false in is_nested_declaration()
        error::instance() << n->get_position()
                << "found function \"" << term->get_string() << "()\", which may be a valid selector token but only if immediately preceeded by one ':' (term)."
                << error_mode_t::ERROR_ERROR;
        return false;

    default:
        error::instance() << n->get_position()
                << "found token " << term->get_type() << ", which is not a valid selector token (term)."
                << error_mode_t::ERROR_ERROR;
        return false;

    }

    // move on to the next term
    ++pos;

    return true;
}

bool compiler::selector_list(node::pointer_t n, size_t & pos)
{
    // we must have a term first
    if(!selector_term(n, pos))
    {
        return false;
    }

    for(;;)
    {
        if(pos >= n->size())
        {
            return true;
        }

        // skip whitespaces between terms
        // this also works for binary operators
        node::pointer_t term(n->get_child(pos));
        if(term->is(node_type_t::WHITESPACE))
        {
            ++pos;

            // end of list too soon?
            if(pos >= n->size())
            {
                // this should not happen since we remove leading/trailing
                // white space tokens
                throw csspp_exception_logic("compiler.cpp: a component value has a WHITESPACE token before the OPEN_CURLYBRACKET."); // LCOV_EXCL_LINE
            }
            term = n->get_child(pos);
        }

        if(term->is(node_type_t::GREATER_THAN)
        || term->is(node_type_t::ADD)
        || term->is(node_type_t::PRECEDED))
        {
            // if we had a WHITESPACE just before the binary operator,
            // remove it as it is not necessary
            if(n->get_child(pos - 1)->is(node_type_t::WHITESPACE))
            {
                n->remove_child(pos - 1);
            }
            else
            {
                // otherwise just go over that operator
                ++pos;
            }

            // it is mandatory for these tokens to be followed by another
            // term (i.e. binary operators)
            if(pos >= n->size())
            {
                error::instance() << n->get_position()
                        << "found token " << term->get_type() << ", which is expected to be followed by another selector term."
                        << error_mode_t::ERROR_ERROR;
                return false;
            }

            // we may have a WHITESPACE first, if so skip it
            term = n->get_child(pos);
            if(term->is(node_type_t::WHITESPACE))
            {
                // no need before/after binary operators
                n->remove_child(term);

                // end of list too soon?
                if(pos >= n->size())
                {
                    // this should not happen since we remove leading/trailing
                    // white space tokens
                    throw csspp_exception_logic("compiler.cpp: a component value has a WHITESPACE token before the OPEN_CURLYBRACKET."); // LCOV_EXCL_LINE
                }
            }
        }

        if(!selector_term(n, pos))
        {
            return false;
        }
    }
}

bool compiler::parse_selector(node::pointer_t n)
{
    if(!parser::argify(n))
    {
        return false;
    }

    size_t const max_children(n->size());
    for(size_t idx(0); idx < max_children; ++idx)
    {
        node::pointer_t arg(n->get_child(idx));
        if(arg->is(node_type_t::OPEN_CURLYBRACKET))
        {
            // this is at the end of the list, so we're done
            break;
        }
        if(!arg->is(node_type_t::ARG))
        {
            throw csspp_exception_logic("compiler.cpp: parse_selector() just called argify() and yet a child is not an ARG."); // LCOV_EXCL_LINE
        }
        size_t pos(0);
        if(!selector_list(arg, pos))
        {
            return false;
        }

        // check and make sure that #<id> is not repeated in the same
        // list, because that's an error (TBD--there may be one exception
        // now that we have the ~ operator...)
        bool err(false);
        std::map<std::string, bool> hash;
        for(size_t j(0); j < arg->size(); ++j)
        {
            node::pointer_t child(arg->get_child(j));
            if(child->is(node_type_t::HASH))
            {
                if(hash.find(child->get_string()) != hash.end())
                {
                    error::instance() << arg->get_position()
                            << "found #"
                            << child->get_string()
                            << " twice in selector: \""
                            << arg->to_string(0)
                            << "\"."
                            << error_mode_t::ERROR_ERROR;
                    err = true;
                }
                else
                {
                    hash[child->get_string()] = true;
                }
            }
        }
        if(!err)
        {
            if(hash.size() > 1)
            {
                error::instance() << arg->get_position()
                        << "found multiple #id entries, note that in most cases, assuming your HTML is proper (identifiers are not repeated) then only the last #id is necessary."
                        << error_mode_t::ERROR_INFO;
            }
            // This is a valid case... as in:
            //
            //   .settings.active #id
            //   .settings.inactive #id
            //
            // In most cases, though, people do it wrong, if you use #id by
            // itself, it gives you direct access to exactly the right place.
            //
            //else if(hash.size() == 1 && !arg->get_child(0)->is(node_type_t::HASH))
            //{
            //    error::instance() << arg->get_position()
            //            << "found an #id entry which is not at the beginning of the list of selectors; unless your HTML changes that much, #id should be the first selector only."
            //            << error_mode_t::ERROR_INFO;
            //}
        }
    }

    return true;
}

std::string compiler::find_file(std::string const & script_name)
{
    return f_state.find_file(script_name);
}

void compiler::set_validation_script(std::string const & script_name)
{
    // try the filename as is first
    std::string filename(find_file(script_name));
    if(filename.empty())
    {
        if(script_name.substr(script_name.size() - 5) != ".scss")
        {
            // try again with the "scss" extension
            filename = find_file(script_name + ".scss");
        }
    }

    if(filename.empty())
    {
        // a validation script should always be available, right?
        position pos(script_name);
        error::instance() << pos
                << "validation script \""
                << script_name
                << "\" was not found."
                << error_mode_t::ERROR_FATAL;
        throw csspp_exception_exit(1);
    }

    node::pointer_t script;

    // TODO: review whether a cache would be useful, at this point
    //       it does not work because the compiler is destructive.
    //       maybe use node::clone() to make a copy of the cache?
    //auto cache(f_validator_scripts.find(filename));
    //if(cache == f_validator_scripts.end())
    {
        position pos(filename);

        // the file exists, read it now
        std::ifstream in;
        in.open(filename);
        if(!in)
        {
            // a validation script should always be available, right?
            //
            // At this point I do not see how to write a test to hit
            // these lines (i.e. have a file that's accessible in
            // read mode, but cannot be opened)
            error::instance() << pos                        // LCOV_EXCL_LINE
                    << "validation script \""               // LCOV_EXCL_LINE
                    << script_name                          // LCOV_EXCL_LINE
                    << "\" could not be opened."            // LCOV_EXCL_LINE
                    << error_mode_t::ERROR_FATAL;           // LCOV_EXCL_LINE
            throw csspp_exception_exit(1);                  // LCOV_EXCL_LINE
        }

        lexer::pointer_t l(new lexer(in, pos));
        parser p(l);
        script = p.stylesheet();

        // TODO: test whether errors occurred while reading the script, if
        //       so then we have to generate a FATAL error here

        // cache the script
        //f_validator_scripts[filename] = script;
//std::cerr << "script " << filename << " is:\n" << *script;
    }
    //else
    //{
    //    script = cache->second;
    //}

    f_current_validation_script = script;
    script->clear_variables();
}

void compiler::add_validation_variable(std::string const & variable_name, node::pointer_t value)
{
    if(!f_current_validation_script)
    {
        throw csspp_exception_logic("compiler.cpp: somehow add_validation_variable() was called without a current validation script set."); // LCOV_EXCL_LINE
    }

    node::pointer_t var(new node(node_type_t::VARIABLE, value->get_position()));
    var->set_string(variable_name);
    node::pointer_t v(new node(node_type_t::LIST, value->get_position()));
    v->add_child(var);
    v->add_child(value);

    f_current_validation_script->set_variable(variable_name, v);
}

bool compiler::run_validation(bool check_only)
{
    // forbid validations from within validation scripts
    if(f_compiler_validating)
    {
        throw csspp_exception_logic("compiler.cpp:compiler::run_validation(): already validating, cannot validate from within a validation script."); // LCOV_EXCL_LINE
    }

    // save the number of errors so we can test after we ran
    // the compile() function
    error_happened_t old_count;

    safe_compiler_state_t safe_state(f_state);
    f_state.set_root(f_current_validation_script);
    if(check_only)
    {
        // save the current error/warning counters so they do not change
        // on this run
        safe_error_t safe_error;

        // replace the output stream with a memory buffer so the user
        // does not see any of it
        std::stringstream ignore;
        safe_error_stream_t safe_output(ignore);

        // now compile that true/false check
        compile(true);

        // WARNING: this MUST be here (before the closing curly bracket)
        //          and not after the if() since we restore the error
        //          state from before the compile() call.
        //
        bool const result(!old_count.error_happened());

        // now restore the stream and error counters
        return result;
    }

    compile(true);

    return !old_count.error_happened();
}

void compiler::expand_nested_components(node::pointer_t n)
{
    safe_parents_t safe_parents(f_state, n);

    switch(n->get_type())
    {
    case node_type_t::COMPONENT_VALUE:
        {
            node::pointer_t rule_last(n);
            for(size_t idx(0); idx < n->size();)
            {
                node::pointer_t child(n->get_child(idx));
                expand_nested_rules(f_state.get_previous_parent(), n, rule_last, child);
                if(idx < n->size()
                && child == n->get_child(idx))
                {
                    ++idx;
                }
            }
        }
        break;

    case node_type_t::DECLARATION:
        // this is true for all but one case, when @-keyword accepts
        // declarations instead of rules (like @font-face); we may want
        // to test that and use the correct call in the @-keyword...
        //error::instance() << n->get_position()
        //        << "a declaration can only appears inside a rule."
        //        << error_mode_t::ERROR_ERROR;
        for(size_t idx(0); idx < n->size();)
        {
            node::pointer_t child(n->get_child(idx));
            node::pointer_t declaration_root(n);
            expand_nested_declarations(n->get_string(), f_state.get_previous_parent(), declaration_root, child);
            if(idx < n->size()
            && child == n->get_child(idx))
            {
                ++idx;
            }
        }
        //if(n->empty())
        //{
        //    f_state.get_previous_parent()->remove_child(n);
        //}
        break;

    case node_type_t::LIST:
    case node_type_t::AT_KEYWORD:
    case node_type_t::OPEN_CURLYBRACKET:
        for(size_t idx(0); idx < n->size();)
        {
            node::pointer_t child(n->get_child(idx));
            expand_nested_components(child);
            if(idx < n->size()
            && child == n->get_child(idx))
            {
                ++idx;
            }
        }
        break;

    //case node_type_t::ARG: -- we should not have sub-declarations under ARG
    default:
        break;

    }
}

void compiler::expand_nested_rules(node::pointer_t parent, node::pointer_t root, node::pointer_t & last, node::pointer_t n)
{
    safe_parents_t safe_parents(f_state, n);

    switch(n->get_type())
    {
    case node_type_t::COMPONENT_VALUE:
        //
        // before this expansion the declarations are like:
        //
        //    COMPONENT_VALUE
        //      ARG
        //        ...
        //      OPEN_CURLYBRACKET
        //        LIST
        //          DECLARATION
        //            ARG
        //              ...
        //          DECLARATION
        //            ARG
        //              ...
        //          COMPONENT_VALUE  <-- expand this one with the first one
        //            ARG
        //              ...
        //            OPEN_CURLYBRACKET
        //              ...
        //
        // so what we do is move the sub-declaration at the same level as the
        // parent and prepend the name of the parent + "-".
        //
        {
            // move the rule as a child of the parent node
            f_state.get_previous_parent()->remove_child(n);
            size_t pos(parent->child_position(last));
            parent->insert_child(pos + 1, n);

            // prepend the arguments of root to the arguments of n
            // note that this is a product, if root has 3 ARGs and
            // n also has 3 ARGs, we end up with 9 ARGs in n
            node::pointer_t list(new node(node_type_t::LIST, n->get_position()));
            while(!n->empty())
            {
                node::pointer_t child(n->get_child(0));
                if(!child->is(node_type_t::ARG))
                {
                    break;
                }
                n->remove_child(0);
                list->add_child(child);
            }
            for(size_t idx(0); idx < root->size(); ++idx)
            {
                node::pointer_t child(root->get_child(idx));
                if(!child->is(node_type_t::ARG))
                {
                    break;
                }
                // we use clone because of the product
                node::pointer_t clone(child->clone());
                for(size_t l(0); l < list->size(); ++l)
                {
                    node::pointer_t arg(list->get_child(l));
                    // we use clone because of the product
                    for(size_t a(0); a < arg->size(); ++a)
                    {
                        node::pointer_t item(arg->get_child(a));
                        if(!item->is(node_type_t::REFERENCE))
                        {
                            if(a == 0)
                            {
                                node::pointer_t whitespace(new node(node_type_t::WHITESPACE, clone->get_position()));
                                clone->add_child(whitespace);
                            }
                            clone->add_child(item->clone());
                        }
                    }
                }
                n->insert_child(idx, clone);
            }

            last = n;

            for(size_t idx(0); idx < n->size();)
            {
                node::pointer_t child(n->get_child(idx));
                node::pointer_t rule_root(n);
                expand_nested_rules(parent, rule_root, last, child);
                if(idx < n->size()
                && child == n->get_child(idx))
                {
                    ++idx;
                }
            }
        }
        break;

    case node_type_t::DECLARATION:
        for(size_t idx(0); idx < n->size();)
        {
            node::pointer_t child(n->get_child(idx));
            node::pointer_t declaration_root(n);
            expand_nested_declarations(n->get_string(), f_state.get_previous_parent(), declaration_root, child);
            if(idx < n->size()
            && child == n->get_child(idx))
            {
                ++idx;
            }
        }
        if(n->empty())
        {
            f_state.get_previous_parent()->remove_child(n);
        }
        break;

    case node_type_t::AT_KEYWORD:
        for(size_t idx(0); idx < n->size();)
        {
            node::pointer_t child(n->get_child(idx));
            expand_nested_components(child);
            if(idx < n->size()
            && child == n->get_child(idx))
            {
                ++idx;
            }
        }
        break;

    case node_type_t::LIST:
    case node_type_t::OPEN_CURLYBRACKET:
        for(size_t idx(0); idx < n->size();)
        {
            node::pointer_t child(n->get_child(idx));
            expand_nested_rules(parent, root, last, child);
            if(idx < n->size()
            && child == n->get_child(idx))
            {
                ++idx;
            }
        }
        break;

    //case node_type_t::ARG: -- we should not have sub-declarations under ARG
    default:
        break;

    }
}

void compiler::expand_nested_declarations(std::string const & name, node::pointer_t parent, node::pointer_t & root, node::pointer_t n)
{
    safe_parents_t safe_parents(f_state, n);

    switch(n->get_type())
    {
    case node_type_t::DECLARATION:
        //
        // before this expansion the declarations are like:
        //
        //    DECLARATION
        //      ARG
        //        ...
        //    OPEN_CURLYBRACKET
        //      LIST
        //        DECLARATION   <-- expand this one with the first one
        //          ARG
        //            ...
        //        DECLARATION   <-- expand this too, also with the first one
        //          ARG
        //            ...
        //
        // so what we do is move the sub-declaration at the same level as the
        // parent and prepend the name of the parent + "-".
        //
        {
            std::string const sub_name((name == "-csspp-null" ? "" : name + "-") + n->get_string());

            // move this declaration from where it is now to the root
            f_state.get_previous_parent()->remove_child(n);
            size_t pos(parent->child_position(root));
            parent->insert_child(pos + 1, n);
            n->set_string(sub_name);
            root = n;

            for(size_t idx(0); idx < n->size();)
            {
                node::pointer_t child(n->get_child(idx));
                expand_nested_declarations(sub_name, parent, root, child);
                if(idx < n->size()
                && child == n->get_child(idx))
                {
                    ++idx;
                }
            }

            // remove empty declarations
            //if(n->empty())
            //{
            //    f_state.get_previous_parent()->remove_child(n);
            //}
        }
        break;

    case node_type_t::AT_KEYWORD:
        // we may have to handle declarations within an @-keyword, but
        // it is not a sub-expand-nested-declaration
        throw csspp_exception_logic("compiler.cpp:compiler::expand_nested_declarations(): @-keyword cannot appear within a declaration."); // LCOV_EXCL_LINE
        //for(size_t idx(0); idx < n->size();)
        //{
        //    node::pointer_t child(n->get_child(idx));
        //    expand_nested_components(child);
        //    if(idx < n->size()
        //    && child == n->get_child(idx))
        //    {
        //        ++idx;
        //    }
        //}
        //break;

    case node_type_t::LIST:
    case node_type_t::OPEN_CURLYBRACKET:
        for(size_t idx(0); idx < n->size();)
        {
            node::pointer_t child(n->get_child(idx));
            expand_nested_declarations(name, parent, root, child);
            if(idx < n->size()
            && child == n->get_child(idx))
            {
                ++idx;
            }
        }
        if(n->empty())
        {
            f_state.get_previous_parent()->remove_child(n);
        }
        break;

    case node_type_t::COMPONENT_VALUE:
        error::instance() << n->get_position()
                << "a nested declaration cannot include a rule."
                << error_mode_t::ERROR_ERROR;
        break;

    //case node_type_t::ARG: -- we should not have sub-declarations under ARG
    default:
        break;

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
