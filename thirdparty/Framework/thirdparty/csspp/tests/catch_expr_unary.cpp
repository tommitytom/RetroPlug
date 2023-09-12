// Copyright (c) 2015-2022  Made to Order Software Corp.  All Rights Reserved
//
// https://snapwebsites.org/project/csspp
// contact@m2osw.com
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
 * \brief Test the expression.cpp file for all possible unary expressions.
 *
 * This test runs a battery of tests agains the expression.cpp unary
 * expressions to ensure full coverage and that all possible input types
 * are checked for the unary CSS Preprocessor extensions.
 *
 * Note that all the tests use the full chain: lexer, parser, compiler,
 * and assembler to make sure the results are correct. So these tests
 * exercise the assembler even more than the assembler tests, except that
 * it only checks that compressed results are correct instead of all
 * output modes, since its only goal is covering all the possible
 * expression cases and not the assembler, compiler, parser, and lexer
 * classes.
 */

// csspp
//
#include    <csspp/assembler.h>
#include    <csspp/compiler.h>
#include    <csspp/exception.h>
#include    <csspp/parser.h>


// self
//
#include    "catch_main.h"


// C++
//
#include    <sstream>


// last include
//
#include    <snapdev/poison.h>



CATCH_TEST_CASE("Unary expressions", "[expression] [unary]")
{
    CATCH_START_SECTION("integer, identifier, hash color, color")
    {
        std::stringstream ss;
        ss << "$zzzrv: $_csspp_major > 0;\n"
           << "$zzzempty: null;\n"
           << "div {\n"
           << "  border: 3px solid #f1a932;\n"          // unary hash color
           << "  z-index: red(complement(#56af9b));\n"  // direct unary color
           << "  content: \"\\201c\";\n" // open quotation
           << "  width: 13%;\n"
           << "  height: 12.5px;\n"
           << "  color: if($zzzrv, #341109, white);\n"
           << "  background-x: ( 32px + 44px );\n"
           << "  background-y: + 44 * 1px;\n"
           << "  border-top-left-radius: (- 33 + 57) * 2;\n"
           << "  border-top-right-radius: (- 33.5 + 5.7) * 2;\n"
           << "  unicode-range: U+410-417;\n"
           << "  font-size: if(true, 12pt, 12px);\n"
           << "  font-weight: if(false, bold, thin);\n"
           << "  margin-left: calc(5% - 5px);\n"
           << "  margin-right: expression(2.5% + 2px);\n"
           << "  margin-top: 1.5%;\n"
           << "  margin-bottom: - 3.2%;\n"
           << "}\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        // to verify that the result is still an INTEGER we have to
        // test the root node here
        std::stringstream compiler_out;
        compiler_out << *n;
        VERIFY_TREES(compiler_out.str(),

"LIST\n"
+ csspp_test::get_default_variables() +
"    V:zzzempty\n"
"      LIST\n"
"        VARIABLE \"zzzempty\"\n"
"        NULL_TOKEN\n"
"    V:zzzrv\n"
"      LIST\n"
"        VARIABLE \"zzzrv\"\n"
"        LIST\n"
"          INTEGER \"\" I:1\n"
"          WHITESPACE\n"
"          GREATER_THAN\n"
"          WHITESPACE\n"
"          INTEGER \"\" I:0\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      LIST\n"
"        DECLARATION \"border\"\n"
"          ARG\n"
"            INTEGER \"px\" I:3\n"
"            WHITESPACE\n"
"            IDENTIFIER \"solid\"\n"
"            WHITESPACE\n"
"            COLOR H:ff32a9f1\n"
"        DECLARATION \"z-index\"\n"
"          ARG\n"
"            INTEGER \"\" I:175\n"
"        DECLARATION \"content\"\n"
"          ARG\n"
"            STRING \"\xe2\x80\x9c\"\n"
"        DECLARATION \"width\"\n"
"          ARG\n"
"            PERCENT D:0.13\n"
"        DECLARATION \"height\"\n"
"          ARG\n"
"            DECIMAL_NUMBER \"px\" D:12.5\n"
"        DECLARATION \"color\"\n"
"          ARG\n"
"            COLOR H:ff091134\n"
"        DECLARATION \"background-x\"\n"
"          ARG\n"
"            INTEGER \"px\" I:76\n"
"        DECLARATION \"background-y\"\n"
"          ARG\n"
"            INTEGER \"px\" I:44\n"
"        DECLARATION \"border-top-left-radius\"\n"
"          ARG\n"
"            INTEGER \"\" I:48\n"
"        DECLARATION \"border-top-right-radius\"\n"
"          ARG\n"
"            DECIMAL_NUMBER \"\" D:-55.6\n"
"        DECLARATION \"unicode-range\"\n"
"          ARG\n"
"            UNICODE_RANGE I:4496830759952\n"
"        DECLARATION \"font-size\"\n"
"          ARG\n"
"            INTEGER \"pt\" I:12\n"
"        DECLARATION \"font-weight\"\n"
"          ARG\n"
"            IDENTIFIER \"thin\"\n"
"        DECLARATION \"margin-left\"\n"
"          ARG\n"
"            FUNCTION \"calc\"\n"
"              ARG\n"
"                PERCENT D:0.05\n"
"                WHITESPACE\n"
"                SUBTRACT\n"
"                WHITESPACE\n"
"                INTEGER \"px\" I:5\n"
"        DECLARATION \"margin-right\"\n"
"          ARG\n"
"            FUNCTION \"expression\"\n"
"              ARG\n"
"                PERCENT D:0.025\n"
"                WHITESPACE\n"
"                ADD\n"
"                WHITESPACE\n"
"                INTEGER \"px\" I:2\n"
"        DECLARATION \"margin-top\"\n"
"          ARG\n"
"            PERCENT D:0.015\n"
"        DECLARATION \"margin-bottom\"\n"
"          ARG\n"
"            PERCENT D:-0.032\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{"
  "border:3px solid #f1a932;"
  "z-index:175;"
  "content:\"\xe2\x80\x9c\";"
  "width:13%;height:12.5px;"
  "color:#341109;"
  "background-x:76px;"
  "background-y:44px;"
  "border-top-left-radius:48;"
  "border-top-right-radius:-55.6;"
  "unicode-range:U+410-417;"
  "font-size:12pt;"
  "font-weight:thin;"
  "margin-left:calc(5% - 5px);"
  "margin-right:expression(2.5% + 2px);"
  "margin-top:1.5%;"
  "margin-bottom:-3.2%"
"}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("null token (can't output)")
    {
        std::stringstream ss;
        ss << "div{width:null}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        // to verify that the result is still an INTEGER we have to
        // test the root node here
        std::stringstream compiler_out;
        compiler_out << *n;
        VERIFY_TREES(compiler_out.str(),

"LIST\n"
+ csspp_test::get_default_variables() +
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"width\"\n"
"        ARG\n"
"          NULL_TOKEN\n"
+ csspp_test::get_close_comment(true)

            );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("important width")
    {
        std::stringstream ss;
        ss << "div{width:3px !important}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        // to verify that the result is still an INTEGER we have to
        // test the root node here
        std::stringstream compiler_out;
        compiler_out << *n;
        VERIFY_TREES(compiler_out.str(),

"LIST\n"
+ csspp_test::get_default_variables() +
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"width\" F:important\n"
"        ARG\n"
"          INTEGER \"px\" I:3\n"
+ csspp_test::get_close_comment(true)

            );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Variables in expressions", "[expression] [unary] [variable]")
{
    CATCH_START_SECTION("set expression variable and reuse")
    {
        std::stringstream ss;
        ss << "div {\n"
           << "  border: (w := 54px, w / 2.7)[-1] solid #f1a932;\n"
           << "}\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        // to verify that the result is still an INTEGER we have to
        // test the root node here
        std::stringstream compiler_out;
        compiler_out << *n;
        VERIFY_TREES(compiler_out.str(),

"LIST\n"
+ csspp_test::get_default_variables() +
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"border\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"px\" D:20\n"
"          WHITESPACE\n"
"          IDENTIFIER \"solid\"\n"
"          WHITESPACE\n"
"          COLOR H:ff32a9f1\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==

"div{"
  "border:20px solid #f1a932"
"}\n"
+ csspp_test::get_close_comment()

                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Invalid unary expressions", "[expression] [unary] [invalid]")
{
    CATCH_START_SECTION("not a unary token")
    {
        std::stringstream ss;
        ss << "div { border: ?; }\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("test.css(1): error: unsupported type CONDITIONAL as a unary expression token.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("invalid # color")
    {
        std::stringstream ss;
        ss << "div { border: #identifier; }\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("test.css(1): error: the color in #identifier is not valid.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("not a valid token for minus")
    {
        std::stringstream ss;
        ss << "div { border: - \"test\"; }\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("test.css(1): error: unsupported type STRING for operator '-'.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("improper initialization of an expression object")
    {
        csspp::node::pointer_t null_node;
        CATCH_REQUIRE_THROWS_AS(new csspp::expression(null_node), csspp::csspp_exception_logic);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("important width, wrong order")
    {
        std::stringstream ss;
        ss << "div { width: !important 3px; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("test.css(1): warning: A special flag, !important in this case, must only appear at the end of a declaration.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("minus by itself")
    {
        std::stringstream ss;
        ss << "div { width: -; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        VERIFY_ERRORS("test.css(1): error: unsupported type EOF_TOKEN as a unary expression token.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    // no error left over
    VERIFY_ERRORS("");
}

// vim: ts=4 sw=4 et
