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
 * \brief Test the expression.cpp file: "**" operator.
 *
 * This test runs a battery of tests agains the expression.cpp "**" (power)
 * operator to ensure full coverage and that all possible left
 * hand side and right hand side types are checked for the power
 * CSS Preprocessor extensions.
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



CATCH_TEST_CASE("Expression number ** number", "[expression] [power]")
{
    CATCH_START_SECTION("no units, integers")
    {
        std::stringstream ss;
        ss << "div { z-index: 10 ** 3; }";
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
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:1000\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{z-index:1000}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("no units, integers two powers, parenthesis right")
    {
        std::stringstream ss;
        ss << "div { z-index: 4 ** (3 ** 2); }";
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
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:262144\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{z-index:262144}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("no units, integers two powers, parenthesis left")
    {
        std::stringstream ss;
        ss << "div { z-index: (4 ** 3) ** 2; }";
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
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:4096\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{z-index:4096}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("px unit, integers")
    {
        std::stringstream ss;
        ss << "div { width: 10px ** 3 / 2px\\*px; }";
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
"          INTEGER \"px\" I:500\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{width:500px}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("no units, decimal number / integer")
    {
        std::stringstream ss;
        ss << "div { z-index: 7.3 ** 3; }";
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
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"\" D:389.017\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{z-index:389.017}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("no units, integer and decimal number")
    {
        std::stringstream ss;
        ss << "div { z-index: 7 ** 3.1; }";
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
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"\" D:416.681\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{z-index:416.681}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("no units, decimal number / negative power")
    {
        std::stringstream ss;
        ss << "div { z-index: 0.3 ** -3.2; }";
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
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"\" D:47.121\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{z-index:47.121}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("px unit, decimal number / integer")
    {
        std::stringstream ss;
        ss << "div { width: 7.5px pow 3 / 3px\\*px; }";
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
"          DECIMAL_NUMBER \"px\" D:140.625\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{width:140.625px}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("px/em unit, decimal number / integer")
    {
        std::stringstream ss;
        ss << "div { width: (15.0px div 2.0em) pow 3; }";
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
"          DECIMAL_NUMBER \"px * px * px / em * em * em\" D:421.875\n"
+ csspp_test::get_close_comment(true)

            );

//         std::stringstream assembler_out;
//         csspp::assembler a(assembler_out);
//         a.output(n, csspp::output_mode_t::COMPRESSED);
// 
// //std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";
// 
//         CATCH_REQUIRE(assembler_out.str() ==
// "div{width:421.875px}\n"
// + csspp_test::get_close_comment()
//                 );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("px/em unit, decimal number / integer")
    {
        std::stringstream ss;
        ss << "div { width: (50.0px div 2.0em) pow -2; }";
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
"          DECIMAL_NUMBER \"em * em / px * px\" D:0.002\n"
+ csspp_test::get_close_comment(true)

            );

//         std::stringstream assembler_out;
//         csspp::assembler a(assembler_out);
//         a.output(n, csspp::output_mode_t::COMPRESSED);
// 
// //std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";
// 
//         CATCH_REQUIRE(assembler_out.str() ==
// "div{width:421.875px}\n"
// + csspp_test::get_close_comment()
//                 );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("px unit, decimal number / -integer")
    {
        std::stringstream ss;
        ss << "div { width: .75px pow -3 * 3px\\*px\\*px\\*px; }";
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
"          DECIMAL_NUMBER \"px\" D:7.111\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{width:7.111px}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("no units, decimal numbers")
    {
        std::stringstream ss;
        ss << "div { z-index: 7.3 ** 3.1; }";
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
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"\" D:474.571\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{z-index:474.571}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("px unit, decimal numbers")
    {
        std::stringstream ss;
        ss << "div { width: 7.5px pow 3.0 / 3px\\*px; }";
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
"          DECIMAL_NUMBER \"px\" D:140.625\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{width:140.625px}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("% and integer")
    {
        std::stringstream ss;
        ss << "div { width: 105% pow 4; }";
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
"          PERCENT D:1.216\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{width:121.551%}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("% and decimal numbers")
    {
        std::stringstream ss;
        ss << "div { width: 107.5% pow 5.3; }";
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
"          PERCENT D:1.467\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{width:146.712%}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Expression number/invalid ** number/invalid", "[expression] [power] [invalid]")
{
    CATCH_START_SECTION("just ? is not a valid number")
    {
        std::stringstream ss;
        ss << "div { border: ?; }";
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

    CATCH_START_SECTION("number ** ? is invalid")
    {
        std::stringstream ss;
        ss << "div { width: 10px ** ?; }";
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

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Power expressions with invalid dimensions or decimal numbers", "[expression] [power] [invalid]")
{
    CATCH_START_SECTION("right hand side cannot have a dimension")
    {
        std::stringstream ss;
        ss << "div { border: 3px ** 2em; }";
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

        VERIFY_ERRORS("test.css(1): error: the number representing the power cannot be a dimension (em); it has to be unitless.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("power cannot be a percent")
    {
        std::stringstream ss;
        ss << "div { z-index: 10 ** 5%; }";
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

        VERIFY_ERRORS("test.css(1): error: incompatible types between INTEGER and PERCENT for operator '**'.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("power must be integral if left hand side has a dimension")
    {
        std::stringstream ss;
        ss << "div { width: 7.3px ** 3.1; }";
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

        VERIFY_ERRORS("test.css(1): error: a number with a dimension only supports integers as their power (i.e. 3px ** 2 is fine, 3px ** 2.1 is not supported).\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("power zero with a dimension is not allowed")
    {
        std::stringstream ss;
        ss << "div { width: 7.5px ** 0.0; }";
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

        VERIFY_ERRORS("test.css(1): error: a number with a dimension power zero cannot be calculated (i.e. 3px ** 0 = 1 what?).\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("power too large")
    {
        std::stringstream ss;
        ss << "div { width: 7.5px ** 1584.000; }";
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

        VERIFY_ERRORS("test.css(1): error: a number with a dimension power 101 or more would generate a very large string so we refuse it at this time. You may use unitless numbers instead.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("'a ** b ** c' is not valid")
    {
        std::stringstream ss;
        ss << "div { z-index: 4 ** 3 ** 2; }";
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

        VERIFY_ERRORS("test.css(1): error: unsupported type POWER as a unary expression token.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    // no error left over
    VERIFY_ERRORS("");
}

// vim: ts=4 sw=4 et
