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
 * \brief Implementation of the CSS Preprocessor assembler.
 *
 * The CSS Preprocessor assembler generates the output files from whatever
 * the compiler generated.
 *
 * The assembler supports modes that allows one to define how the data is
 * output. The mode uses an internally defined class to handle the
 * formatting.
 *
 * \sa \ref lexer_rules
 */

// self
//
#include    "csspp/assembler.h"

#include    "csspp/exception.h"
#include    "csspp/lexer.h"
#include    "csspp/nth_child.h"
#include    "csspp/unicode_range.h"


// C++
//
#include    <iostream>


// last include
//
//#include    <snapdev/poison.h>



namespace csspp
{

namespace
{

typedef uint32_t        flags_t;

flags_t const g_flag_optional_operator                  = 0x01;
flags_t const g_flag_optional_spaces                    = 0x02;
flags_t const g_flag_optional_space_before              = 0x04;
flags_t const g_flag_optional_space_after               = 0x08;
flags_t const g_flag_optional_spaces_or_newlines        = 0x10;
flags_t const g_flag_optional_space_before_or_newline   = 0x20;
flags_t const g_flag_optional_space_after_or_newline    = 0x40;

void verify_dimension(node::pointer_t n)
{
    std::string const dimension(n->get_string());
    std::string::size_type pos(dimension.find_first_of(" */"));
    if(pos != std::string::npos)
    {
        error::instance() << n->get_position()
                << "\""
                << dimension
                << "\" is not a valid CSS dimension."
                << error_mode_t::ERROR_ERROR;
    }
}

} // no name namespace

// base class
class assembler_impl
{
public:
    assembler_impl(std::ostream & out)
        : f_out(out)
    {
    }

    virtual ~assembler_impl()
    {
    }

    virtual void output_string(std::string const & str)
    {
        if(!str.empty())
        {
            f_started = str.back() != '\n';
        }
        f_out << str;
    }

    virtual void output_operator(std::string const & str, flags_t flags)
    {
        static_cast<void>(flags);

        // the default prints that as is
        if((flags & g_flag_optional_operator) == 0)
        {
            output_string(str);
        }
    }

    virtual void output_token(std::string const & str)
    {
        // the default prints that as is
        output_string(str);
    }

    virtual void newline()
    {
        // by default do not write newlines
    }

    virtual void newline_if_not_empty()
    {
        if(f_started)
        {
            f_started = false;
            f_out << std::endl;
        }
    }

    virtual void output_identation()
    {
        // by default do not write identation
    }

protected:
    std::ostream &          f_out;
    bool                    f_started = false;
};

class assembler_compressed : public assembler_impl
{
public:
    assembler_compressed(std::ostream & out)
        : assembler_impl(out)
    {
    }

    virtual ~assembler_compressed()
    {
    }
};

class assembler_tidy : public assembler_compressed
{
public:
    assembler_tidy(std::ostream & out)
        : assembler_compressed(out)
    {
    }

    virtual ~assembler_tidy()
    {
    }

    virtual void newline()
    {
        f_started = false;
        f_out << std::endl;
    }
};

class assembler_compact : public assembler_tidy
{
public:
    assembler_compact(std::ostream & out)
        : assembler_tidy(out)
    {
    }

    virtual ~assembler_compact()
    {
    }

    virtual void output_operator(std::string const & str, flags_t flags)
    {
        f_started = true;
        if((flags & (g_flag_optional_spaces | g_flag_optional_spaces_or_newlines)) != 0)
        {
            f_out << " " << str << " ";
        }
        else if((flags & (g_flag_optional_space_before | g_flag_optional_space_before_or_newline)) != 0)
        {
            f_out << " " << str;
        }
        else if((flags & (g_flag_optional_space_after | g_flag_optional_space_after_or_newline)) != 0)
        {
            f_out << str << " ";
        }
        else
        {
            assembler_tidy::output_operator(str, flags);
        }
    }
};

class assembler_expanded : public assembler_compact
{
public:
    assembler_expanded(std::ostream & out)
        : assembler_compact(out)
    {
    }

    virtual ~assembler_expanded()
    {
    }

    virtual void output_operator(std::string const & str, flags_t flags)
    {
        if((flags & g_flag_optional_spaces_or_newlines) != 0)
        {
            f_started = false;
            f_out << std::endl << str << std::endl;
        }
        else if((flags & g_flag_optional_space_before_or_newline) != 0)
        {
            f_started = false;
            f_out << std::endl << str;
        }
        else if((flags & g_flag_optional_space_after_or_newline) != 0)
        {
            f_started = false;
            f_out << str << std::endl;
        }
        else if((flags & g_flag_optional_operator) != 0)
        {
            f_started = true;
            f_out << str;
        }
        else
        {
            assembler_compact::output_operator(str, flags);
        }
    }

    virtual void output_identation()
    {
        f_out << "  ";
    }
};

assembler::assembler(std::ostream & out)
    : f_out(out)
{
}

std::string assembler::escape_id(std::string const & id)
{
    std::string result;

    // create a temporary lexer to apply the conversion
    std::stringstream ss;
    position pos("assembler.css");
    lexer l(ss, pos);

    bool first_char(true);
    for(char const *s(id.c_str()); *s != '\0'; )
    {
        char mb[5];
        unsigned char c(static_cast<unsigned char>(*s));
        size_t len(1);
        if(c >= 0xF0)
        {
            len = 4;
        }
        else if(c >= 0xE0)
        {
            len = 3;
        }
        else if(c >= 0xC0)
        {
            len = 2;
        }
        //else len = 1 -- already set to 1 by default
        for(size_t i(0); i < len; ++i, ++s)
        {
            if(*s == '\0')
            {
                // UTF-8 should be perfect when we reach the assembler
                throw csspp_exception_logic("assembler.cpp: assembler::escape_id(): invalid UTF-8 character found."); // LCOV_EXCL_LINE
            }
            mb[i] = *s;
        }
        mb[len] = '\0';

        wide_char_t wc(l.mbtowc(mb));

        if((first_char && lexer::is_start_identifier(wc))
        || (!first_char && lexer::is_identifier(wc)))
        {
            result += mb;
        }
        else
        {
            result += '\\';
            if(wc >= '0' && wc <= '9')
            {
                // digits need to be defined as hexa
                result += '3';
                result += wc;
                // add a space if the next character requires us to do so
                // (by now identifier letters should all be lower case so
                // the 'A' to 'F' should never match)
                if((s[0] >= '0' && s[0] <= '9')
                || (s[0] >= 'a' && s[0] <= 'f')
                || (s[0] >= 'A' && s[0] <= 'F')) // LCOV_EXCL_LINE
                {
                    result += ' ';
                }
            }
            else
            {
                result += mb;
            }
        }
        first_char = false;
    }

    return result;
}

void assembler::output(node::pointer_t n, output_mode_t mode)
{
    f_root = n;

    f_impl.reset();
    switch(mode)
    {
    case output_mode_t::COMPACT:
        f_impl.reset(new assembler_compact(f_out));
        break;

    case output_mode_t::COMPRESSED:
        f_impl.reset(new assembler_compressed(f_out));
        break;

    case output_mode_t::EXPANDED:
        f_impl.reset(new assembler_expanded(f_out));
        break;

    case output_mode_t::TIDY:
        f_impl.reset(new assembler_tidy(f_out));
        break;

    }
    if(!f_impl)
    {
        throw csspp_exception_logic("assembler.cpp: assembler::output(): called with an invalid mode.");
    }

    output(n);
}

void assembler::output(node::pointer_t n)
{
    switch(n->get_type())
    {
    case node_type_t::ADD:
        f_impl->output_operator("+", g_flag_optional_spaces);
        break;

    case node_type_t::ARG:
        {
            size_t const max_children(n->size());
            for(size_t idx(0); idx < max_children; ++idx)
            {
                output(n->get_child(idx));
            }
        }
        break;

    case node_type_t::AT_KEYWORD:
        output_at_keyword(n);
        break;

    case node_type_t::COLON:
        f_impl->output_operator(":", 0);
        break;

    case node_type_t::COLOR:
        {
            color c(n->get_color());
            f_impl->output_token(c.to_string());
        }
        break;

    // This should have been transformed to a list (ARG for selectors
    // and functions...)
    //case node_type_t::COMMA:
    //    f_impl->output_operator(",", g_flag_optional_space_after);
    //    break;

    case node_type_t::COMMENT:
        output_comment(n);
        break;

    case node_type_t::DASH_MATCH:
        f_impl->output_operator("|=", g_flag_optional_spaces);
        break;

    case node_type_t::DECIMAL_NUMBER:
        // this may be a dimension, if not f_string is empty anyway
        verify_dimension(n);
        f_out << decimal_number_to_string(n->get_decimal_number(), true) << n->get_string();
        break;

    case node_type_t::DECLARATION:
        {
            f_impl->output_identation();
            f_out << n->get_string();
            f_impl->output_operator(":", g_flag_optional_space_after);
            size_t const max_children(n->size());
            for(size_t idx(0); idx < max_children; ++idx)
            {
                node::pointer_t child(n->get_child(idx));
                output(child);
                if(child->is(node_type_t::ARG)
                && idx + 1 != max_children)
                {
                    f_impl->output_operator(",", g_flag_optional_space_after);
                }
            }
            // we make sure it appears at the end
            if(n->get_flag("important"))
            {
                f_impl->output_operator("!", g_flag_optional_space_before);
                f_out << "important";
            }
        }
        break;

    case node_type_t::DIVIDE:
        f_impl->output_operator("/", 0);
        break;

    case node_type_t::EQUAL:
        f_impl->output_operator("=", g_flag_optional_spaces);
        break;

    case node_type_t::FONT_METRICS:
        // this is a mouthful!
        f_out << decimal_number_to_string(n->get_font_size() * (n->get_dim1() == "%" ? 100.0 : 1.0), true) << n->get_dim1()
              << "/" << decimal_number_to_string(n->get_line_height() * (n->get_dim2() == "%" ? 100.0 : 1.0), true) << n->get_dim2();
        break;

    case node_type_t::FUNCTION:
        {
            f_out << n->get_string();
            f_impl->output_operator("(", 0);
            if(!n->empty())
            {
                if(n->get_child(0)->is(node_type_t::ARG))
                {
                    bool first(true);
                    size_t const max_children(n->size());
                    for(size_t idx(0); idx < max_children; ++idx)
                    {
                        if(first)
                        {
                            first = false;
                        }
                        else
                        {
                            f_impl->output_operator(",", g_flag_optional_space_after);
                        }
                        output(n->get_child(idx));
                    }
                }
                else
                {
                    // no ARG then no commas; this happens in :not(),
                    // :lang(), nth-child(), etc.
                    size_t const max_children(n->size());
                    for(size_t idx(0); idx < max_children; ++idx)
                    {
                        output(n->get_child(idx));
                    }
                }
            }
            f_impl->output_operator(")", 0);
        }
        break;

    case node_type_t::GREATER_THAN:
        f_impl->output_operator(">", g_flag_optional_spaces);
        break;

    case node_type_t::HASH:
        f_out << "#" << n->get_string();
        break;

    case node_type_t::IDENTIFIER:
        f_out << escape_id(n->get_string());
        break;

    case node_type_t::INCLUDE_MATCH:
        f_impl->output_operator("~=", g_flag_optional_spaces);
        break;

    case node_type_t::INTEGER:
        // this may be a dimension, if not f_string is empty anyway
        verify_dimension(n);
        f_out << n->get_integer() << n->get_string();
        break;

    case node_type_t::LIST:
        {
            size_t const max_children(n->size());
            for(size_t idx(0); idx < max_children; ++idx)
            {
                node::pointer_t child(n->get_child(idx));
                output(child);
                if(child->is(node_type_t::DECLARATION)
                && idx + 1 != max_children)
                {
                    f_impl->output_operator(";", g_flag_optional_space_after_or_newline);
                }
            }
        }
        break;

    case node_type_t::MULTIPLY:
        f_impl->output_operator("*", 0);
        break;

    case node_type_t::OPEN_CURLYBRACKET:
        {
            f_impl->output_operator("{", g_flag_optional_spaces_or_newlines);
            size_t const max_children(n->size());
            for(size_t idx(0); idx < max_children; ++idx)
            {
                node::pointer_t item(n->get_child(idx));
                output(n->get_child(idx));
                if(item->is(node_type_t::DECLARATION)
                && idx + 1 < max_children)
                {
                    f_impl->output_operator(";", g_flag_optional_space_after_or_newline);
                }
            }
            f_impl->output_operator(";", g_flag_optional_operator);
            f_impl->output_operator("}", g_flag_optional_space_before_or_newline);
            f_impl->newline();
        }
        break;

    case node_type_t::OPEN_PARENTHESIS:
        output_parenthesis(n, 0);
        break;

    case node_type_t::OPEN_SQUAREBRACKET:
        {
            f_impl->output_operator("[", 0);
            size_t const max_children(n->size());
            for(size_t idx(0); idx < max_children; ++idx)
            {
                output(n->get_child(idx));
            }
            f_impl->output_operator("]", 0);
        }
        break;

    case node_type_t::PERCENT:
        f_out << decimal_number_to_string(n->get_decimal_number() * 100.0, true) << "%";
        break;

    case node_type_t::PERIOD:
        f_impl->output_operator(".", 0);
        break;

    case node_type_t::PRECEDED:
        f_impl->output_operator("~", g_flag_optional_spaces);
        break;

    case node_type_t::PREFIX_MATCH:
        f_impl->output_operator("^=", g_flag_optional_spaces);
        break;

    case node_type_t::SCOPE:
        f_impl->output_operator("|", 0);
        break;

    case node_type_t::STRING:
        output_string(n->get_string());
        break;

    case node_type_t::SUBSTRING_MATCH:
        f_impl->output_operator("*=", g_flag_optional_spaces);
        break;

    case node_type_t::SUBTRACT: // for calc() / expression()
        f_impl->output_operator("-", 0);
        break;

    case node_type_t::SUFFIX_MATCH:
        f_impl->output_operator("$=", g_flag_optional_spaces);
        break;

    case node_type_t::UNICODE_RANGE:
        {
            unicode_range_t const range(static_cast<range_value_t>(n->get_integer()));
            f_out << "U+" << range.to_string();
        }
        break;

    case node_type_t::URL:
        // TODO: escape special characters or we won't be able to re-read this one
        output_url(n->get_string());
        break;

    case node_type_t::WHITESPACE:
        // explicit whitespace that we still have in the tree are kept as is
        f_out << " ";
        break;

    case node_type_t::COMPONENT_VALUE:
        output_component_value(n);
        break;

    case node_type_t::AN_PLUS_B:
        {
            // TODO: support adding around the operator?
            nth_child const an_b(n->get_integer());
            f_out << an_b.to_string();
        }
        break;

    case node_type_t::FRAME:
        {
            // output the frame position
            //
            decimal_number_t p(n->get_decimal_number());
            if(p >= 1.0)
            {
                f_out << "to";  // strlen("to") < strlen("100%")!
            }
            else
            {
                // strlen("from") > strlen("0%") so we use "0%"
                //
                if(p < 0.0)
                {
                    p = 0.0;
                }
                f_out << decimal_number_to_string(p * 100.0, true) << "%";
            }

            // output the frame component values
            //
            f_impl->output_operator("{", g_flag_optional_spaces_or_newlines);
            size_t const max_children(n->size());
            for(size_t idx(0); idx < max_children; ++idx)
            {
                node::pointer_t item(n->get_child(idx));
                output(n->get_child(idx));
                if(item->is(node_type_t::DECLARATION)
                && idx + 1 < max_children)
                {
                    f_impl->output_operator(";", g_flag_optional_space_after_or_newline);
                }
            }
            f_impl->output_operator(";", g_flag_optional_operator);
            f_impl->output_operator("}", g_flag_optional_space_before_or_newline);
            f_impl->newline();
        }
        break;

    case node_type_t::UNKNOWN:
    case node_type_t::AND:
    case node_type_t::ASSIGNMENT:
    case node_type_t::ARRAY:
    case node_type_t::BOOLEAN:
    case node_type_t::CDC:
    case node_type_t::CDO:
    case node_type_t::CLOSE_CURLYBRACKET:
    case node_type_t::CLOSE_PARENTHESIS:
    case node_type_t::CLOSE_SQUAREBRACKET:
    case node_type_t::COLUMN:
    case node_type_t::COMMA:
    case node_type_t::CONDITIONAL:
    case node_type_t::DOLLAR:
    case node_type_t::EOF_TOKEN:
    case node_type_t::EXCLAMATION:
    case node_type_t::GREATER_EQUAL:
    case node_type_t::LESS_EQUAL:
    case node_type_t::LESS_THAN:
    case node_type_t::MAP:
    case node_type_t::MODULO:
    case node_type_t::NOT_EQUAL:
    case node_type_t::NULL_TOKEN:
    case node_type_t::PLACEHOLDER:
    case node_type_t::POWER:
    case node_type_t::REFERENCE:
    case node_type_t::SEMICOLON:
    case node_type_t::VARIABLE:
    case node_type_t::VARIABLE_FUNCTION:
    case node_type_t::max_type:
        // many of the nodes are not expected in a valid tree being compiled
        // all of those will generate this exception
        {
            std::stringstream ss;
            ss << "assembler.cpp: unexpected token "
               << n->get_type()
               << " in output() call.";
            throw csspp_exception_logic(ss.str());
        }

    }
}

void assembler::output_component_value(node::pointer_t n)
{
    bool first(true);
    bool has_arg(false);
    size_t const max_children(n->size());
    for(size_t idx(0); idx < max_children; ++idx)
    {
        node::pointer_t c(n->get_child(idx));
        if(c->is(node_type_t::OPEN_CURLYBRACKET))
        {
            if(has_arg)
            {
                output(c);
            }
        }
        else if(!c->is(node_type_t::ARG))
        {
            // unexpected for a component value
            //
            std::stringstream ss;                                                                           // LCOV_EXCL_LINE
            ss << "assembler.cpp: expected all direct children of COMPONENT_VALUE to be ARG instead of "    // LCOV_EXCL_LINE
               << c->get_type()                                                                             // LCOV_EXCL_LINE
               << " on line "
               << c->get_position().get_line()
               << " in \""
               << c->get_position().get_filename()
               << "\".";                                                                                    // LCOV_EXCL_LINE
            if(c->is(node_type_t::IDENTIFIER))
            {
                ss << " (identifier is \"" << escape_id(c->get_string()) << "\")";
            }
            throw csspp_exception_logic(ss.str());                                                          // LCOV_EXCL_LINE
        }
        else if(c->empty() || !c->get_last_child()->is(node_type_t::PLACEHOLDER))
        {
            // TODO: if we compile out PLACEHOLDER nodes in the compiler
            //       then we can remove the test here... (on the line prior)
            has_arg = true;
            if(first)
            {
                first = false;
            }
            else
            {
                f_impl->output_operator(",", g_flag_optional_space_after);
            }
            output(c);
        }
    }
}

void assembler::output_parenthesis(node::pointer_t n, int flags)
{
    if(flags == 0)
    {
        // we must have a space here otherwise the '(' transforms a
        // preceeding identifier in a function
        //
        // TODO: once our assembler is smarter we will know what is
        //       before and thus avoid the space if possible.
        //
        f_out << " ";
    }
    f_impl->output_operator("(", 0);
    size_t const max_children(n->size());
    for(size_t idx(0); idx < max_children; ++idx)
    {
        node::pointer_t child(n->get_child(idx));
        if(child->is(node_type_t::OPEN_PARENTHESIS))
        {
            if(idx != 0)
            {
                f_out << " ";
            }
            output_parenthesis(child, 1);
        }
        else
        {
            output(child);
        }
    }
    f_impl->output_operator(")", flags == 0 ? g_flag_optional_space_after : 0);
}

void assembler::output_at_keyword(node::pointer_t n)
{
    f_out << "@" << n->get_string() << " ";
    bool no_block(true);
    size_t const max_children(n->size());
    if(max_children > 0)
    {
        if(n->get_string() == "-o-keyframes"
        || n->get_string() == "-webkit-keyframes"
        || n->get_string() == "keyframes")
        {
            // in this case we have one identifier followed by X frames
            // which need to appear between '{' ... '}'
            //
            output(n->get_child(0));
            f_impl->output_operator("{", g_flag_optional_space_before);
            f_impl->newline();
            for(size_t idx(1); idx < max_children; ++idx)
            {
                output(n->get_child(idx));
            }
            f_impl->output_operator("}", 0);
            no_block = false;  // do not output ';'
        }
        else
        {
            for(size_t idx(0); idx < max_children; ++idx)
            {
                node::pointer_t child(n->get_child(idx));
                if(idx + 1 == max_children
                && child->is(node_type_t::OPEN_CURLYBRACKET))
                {
                    f_impl->newline();
                    no_block = false;  // do not output ';'
                }
                //if(child->is(node_type_t::COMPONENT_VALUE))
                //{
                //    f_impl->newline();
                //    f_impl->output_operator("{", 0);
                //    f_impl->newline();
                //    output(child);
                //    f_impl->output_operator("}", 0);
                //    f_impl->newline();
                //}
                //else
                if(child->is(node_type_t::OPEN_CURLYBRACKET))
                {
                    // nearly like output(child), except that we do not add
                    // a ';' before the '}'
                    f_impl->output_operator("{", 0);
                    f_impl->newline();
                    size_t const max_sub_children(child->size());
                    for(size_t j(0); j < max_sub_children; ++j)
                    {
                        output(child->get_child(j));
                    }
                    f_impl->output_operator("}", 0);
                    f_impl->newline();
                }
                else if(child->is(node_type_t::ARG))
                {
                    output(child);
                    if(idx + 1 < max_children
                    && n->get_child(idx + 1)->is(node_type_t::ARG))
                    {
                        f_impl->output_operator(",", g_flag_optional_space_after);
                    }
                }
                else
                {
                    output(child);
                }
            }
        }
    }
    if(no_block)
    {
        f_out << ";";
    }

    // extra newline after an @-keyword
    f_impl->newline();
}

void assembler::output_comment(node::pointer_t n)
{
    // we take care of comments and don't give the impl's a chance to
    // do anything about this; (1) we force a newline before if we
    // already output something; (2) we force a newline at the end
    //
    f_impl->newline_if_not_empty();
    std::string const comment(n->get_string());
    if(n->get_integer() == 0)
    {
        // note: a C++ comment is not valid in a .css file, so here we
        //       convert it
        //
        bool first(true);
        std::string::size_type start(0);
        std::string::size_type end(comment.find('\n'));
        while(end != std::string::npos)
        {
            if(first)
            {
                first = false;
                f_out << "/* ";
            }
            else
            {
                f_out << " * ";
            }
            f_out << comment.substr(start, end - start) << std::endl;
            start = end + 1;
            end = comment.find('\n', start);
        }
        if(start < comment.size())
        {
            if(first)
            {
                // write the whole thing on a single line
                f_out << "/* "
                      << comment.substr(start)
                      << " */"
                      << std::endl;
            }
            else
            {
                f_out << " * " << comment.substr(start) << std::endl
                      << " */" << std::endl;
            }
        }
    }
    else
    {
        // TODO: add the " * " on each line? (I don't think we remove
        //       those thus we would already have them if present in the
        //       source)
        //
        f_out << "/* " << comment << " */" << std::endl;
    }
}

void assembler::output_string(std::string const & str)
{
    // count the single and double quotes
    int sq(0);
    int dq(0);
    for(char const *s(str.c_str()); *s != '\0'; ++s)
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

    // more single quotes? if so use "..."
    if(sq >= dq)
    {
        // use " in this case
        f_out << '"';
        for(char const *s(str.c_str()); *s != '\0'; ++s)
        {
            if(*s == '"')
            {
                f_out << "\\\"";
            }
            else
            {
                f_out << *s;
            }
        }
        f_out << '"';
    }
    else
    {
        // use ' in this case
        f_out << '\'';
        for(char const *s(str.c_str()); *s != '\0'; ++s)
        {
            if(*s == '\'')
            {
                f_out << "\\'";
            }
            else
            {
                f_out << *s;
            }
        }
        f_out << '\'';
    }
}

void assembler::output_url(std::string const & str)
{
    f_out << "url";
    f_impl->output_operator("(", g_flag_optional_space_after);

    //
    // the URI can be output as is if it does not include one of:
    //   '('
    //   ')'
    //   "'"
    //   '"'
    //   non-printable character (see lexer)
    //
    bool direct(true);
    for(char const *s(str.c_str()); *s != '\0'; ++s)
    {
        char const c(*s);
        if(c == '('
        || c == ')'
        || c == '\''
        || c == '"'
        || lexer::is_non_printable(static_cast<wide_char_t>(c))) // this is UTF-8 compatible
        {
            direct = false;
            break;
        }
    }
    if(direct)
    {
        // we can output that one as is
        f_out << str;
    }
    else
    {
        // output this URI as a string
        output_string(str);
    }

    f_impl->output_operator(")", g_flag_optional_space_before);
}

} // namespace csspp

std::ostream & operator << (std::ostream & out, csspp::output_mode_t const type)
{
    switch(type)
    {
    case csspp::output_mode_t::COMPACT:
        out << "COMPACT";
        break;

    case csspp::output_mode_t::COMPRESSED:
        out << "COMPRESSED";
        break;

    case csspp::output_mode_t::EXPANDED:
        out << "EXPANDED";
        break;

    case csspp::output_mode_t::TIDY:
        out << "TIDY";
        break;

    }

    return out;
}

// Local Variables:
// mode: cpp
// indent-tabs-mode: nil
// c-basic-offset: 4
// tab-width: 4
// End:

// vim: ts=4 sw=4 et
