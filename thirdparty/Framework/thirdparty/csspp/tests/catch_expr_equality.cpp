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
 * \brief Test the expression.cpp file: "=", "!=", "~=", "^=", "$=",
 * "*=", "|=" operators.
 *
 * This test runs a battery of tests agains the expression.cpp "=" (equal),
 * "!=" (not equal), "~=" (includes word), "^=" (starts with),
 * "$=" (ends with), "*=" (includes substring), |= (includes dashed word)
 * operators to ensure full coverage and that all possible left
 * hand side and right hand side types are checked for the equality
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



CATCH_TEST_CASE("Expression number =,!= number", "[expression] [equality]")
{
    CATCH_START_SECTION("compare 10 = 3")
    {
        std::stringstream ss;
        ss << "div { z-index: 10 = 3 ? 9 : 5; }";
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
"          INTEGER \"\" I:5\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==

"div{z-index:5}\n"
+ csspp_test::get_close_comment()

                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("compare 10 != 3")
    {
        std::stringstream ss;
        ss << "div { z-index: 10 "
           << (rand() & 1 ? "not-equal" : "!=")
           << " 3 ? 9 : 5; }";
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
"          INTEGER \"\" I:9\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==

"div{z-index:9}\n"
+ csspp_test::get_close_comment()

                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("compare 10.2 = 3.1")
    {
        std::stringstream ss;
        ss << "div { z-index: 10.2 = 3.1 ? 9 : 5; }";
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
"          INTEGER \"\" I:5\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==

"div{z-index:5}\n"
+ csspp_test::get_close_comment()

                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("compare 10.2 != 3.1")
    {
        std::stringstream ss;
        ss << "div { z-index: 10.2 != 3.1 ? 9 : 5; }";
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
"          INTEGER \"\" I:9\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==

"div{z-index:9}\n"
+ csspp_test::get_close_comment()

                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("compare 10 = 3.1")
    {
        std::stringstream ss;
        ss << "div { z-index: 10 = 3.1 ? 9 : 5; }";
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
"          INTEGER \"\" I:5\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==

"div{z-index:5}\n"
+ csspp_test::get_close_comment()

                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("compare 10 != 3.1")
    {
        std::stringstream ss;
        ss << "div { z-index: 10 not-equal 3.1 ? 9 : 5; }";
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
"          INTEGER \"\" I:9\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==

"div{z-index:9}\n"
+ csspp_test::get_close_comment()

                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("compare 10.2 = 3")
    {
        std::stringstream ss;
        ss << "div { z-index: 10.2 = 3 ? 9 : 5; }";
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
"          INTEGER \"\" I:5\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==

"div{z-index:5}\n"
+ csspp_test::get_close_comment()

                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("compare 10.2 != 3")
    {
        std::stringstream ss;
        ss << "div { z-index: 10.2 "
           << (rand() & 1 ? "not-equal" : "!=")
           << " 3 ? 9 : 5; }";
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
"          INTEGER \"\" I:9\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==

"div{z-index:9}\n"
+ csspp_test::get_close_comment()

                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("compare (5 = 5) = (7.1 = 7.1)")
    {
        std::stringstream ss;
        ss << "div { z-index: (5 = 5) = (7.1 = 7.1)"
           << " ? 9 : 5; }";
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
"          INTEGER \"\" I:9\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==

"div{z-index:9}\n"
+ csspp_test::get_close_comment()

                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("compare (5 = 5) != (7.1 = 7.1)")
    {
        std::stringstream ss;
        ss << "div { z-index: (5 = 5) != (7.1 = 7.1)"
           << " ? 9 : 5; }";
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
"          INTEGER \"\" I:5\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==

"div{z-index:5}\n"
+ csspp_test::get_close_comment()

                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("compare 7.1% = 7.1%)")
    {
        std::stringstream ss;
        ss << "div { z-index: 7.1% = 7.1% ? 9 : 5; }";
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
"          INTEGER \"\" I:9\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==

"div{z-index:9}\n"
+ csspp_test::get_close_comment()

                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("compare 7.1% != 7.1%)")
    {
        std::stringstream ss;
        ss << "div { z-index: 7.1% != 7.1% ? 9 : 5; }";
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
"          INTEGER \"\" I:5\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==

"div{z-index:5}\n"
+ csspp_test::get_close_comment()

                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Expression color =,!= color", "[expression] [equality]")
{
    CATCH_START_SECTION("compare black = white")
    {
        std::stringstream ss;
        ss << "div { z-index: black = white ? 9 : 5; }";
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
"          INTEGER \"\" I:5\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==

"div{z-index:5}\n"
+ csspp_test::get_close_comment()

                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("compare black = #000")
    {
        std::stringstream ss;
        ss << "div { z-index: black = #000 ? 9 : 5; }";
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
"          INTEGER \"\" I:9\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==

"div{z-index:9}\n"
+ csspp_test::get_close_comment()

                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("compare rgba(r,g,b,a) = rgba(r,g,b,a)")
    {
        csspp::integer_t const r(rand() % 256);
        csspp::integer_t const g(rand() % 256);
        csspp::integer_t const b(rand() % 256);
        csspp::decimal_number_t const alpha(static_cast<csspp::decimal_number_t>(rand() % 1000) / 1000.0);
        std::stringstream ss;
        ss << "div { z-index: rgba("
           << r
           << ","
           << g
           << ","
           << b
           << ","
           << alpha
           << ") = rgba("
           << r
           << ","
           << g
           << ","
           << b
           << ","
           << alpha
           << ") ? 9 : 5; }";
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
"          INTEGER \"\" I:9\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==

"div{z-index:9}\n"
+ csspp_test::get_close_comment()

                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Expression string ~=,^=,$=,*=,|= string", "[expression] [equality]")
{
    char const * colors[]
    {
        "bleu", "blanc", "rouge"
    };

    CATCH_START_SECTION("compare 'blue'/'blanc'/'rouge' ~= 'bleu blanc rouge' -- always true")
    {
        for(size_t idx(0); idx < sizeof(colors) / sizeof(colors[0]); ++idx)
        {
            std::stringstream ss;
            ss << "div { z-index: '"
               << colors[idx]
               << "' ~= 'bleu blanc rouge' ? 9 : 5; }";
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
"          INTEGER \"\" I:9\n"
+ csspp_test::get_close_comment(true)

                );

            std::stringstream assembler_out;
            csspp::assembler a(assembler_out);
            a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

            CATCH_REQUIRE(assembler_out.str() ==

"div{z-index:9}\n"
+ csspp_test::get_close_comment()

                    );

            CATCH_REQUIRE(c.get_root() == n);
        }
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("compare 'blue'/'blanc'/'rouge' ~= 'blue white red' -- always false")
    {
        for(size_t idx(0); idx < sizeof(colors) / sizeof(colors[0]); ++idx)
        {
            std::stringstream ss;
            ss << "div { z-index: '"
               << colors[idx]
               << "' ~= 'blue white red' ? 9 : 5; }";
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
"          INTEGER \"\" I:5\n"
+ csspp_test::get_close_comment(true)

                );

            std::stringstream assembler_out;
            csspp::assembler a(assembler_out);
            a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

            CATCH_REQUIRE(assembler_out.str() ==

"div{z-index:5}\n"
+ csspp_test::get_close_comment()

                    );

            CATCH_REQUIRE(c.get_root() == n);
        }
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("compare 'blue blanc rouge'.substr(...) ^= 'bleu blanc rouge' -- always true")
    {
        std::string const str("bleu blanc rouge");
        for(size_t idx(0); idx < str.length(); ++idx)
        {
            std::stringstream ss;
            ss << "div { z-index: '"
               << str.substr(0, idx)
               << "' ^= 'bleu blanc rouge' ? 9 : 5; }";
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
"          INTEGER \"\" I:9\n"
+ csspp_test::get_close_comment(true)

                );

            std::stringstream assembler_out;
            csspp::assembler a(assembler_out);
            a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

            CATCH_REQUIRE(assembler_out.str() ==

"div{z-index:9}\n"
+ csspp_test::get_close_comment()

                    );

            CATCH_REQUIRE(c.get_root() == n);
        }
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("compare 'blue blanc rouge'.substr(...) ^= 'bleu blanc rouge' -- always false")
    {
        std::string const str("bleu blanc rouge");
        for(size_t idx(1); idx < str.length(); ++idx)
        {
            std::stringstream ss;
            ss << "div { z-index: '"
               << str.substr(1, idx)
               << "' ^= 'bleu blanc rouge' ? 9 : 5; }";
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
"          INTEGER \"\" I:5\n"
+ csspp_test::get_close_comment(true)

                );

            std::stringstream assembler_out;
            csspp::assembler a(assembler_out);
            a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

            CATCH_REQUIRE(assembler_out.str() ==

"div{z-index:5}\n"
+ csspp_test::get_close_comment()

                    );

            CATCH_REQUIRE(c.get_root() == n);
        }
    }
    CATCH_END_SECTION()

    // slight variation
    CATCH_START_SECTION("compare 'bleue' ^= 'bleu blanc rouge' -- always false")
    {
        std::stringstream ss;
        ss << "div { z-index: 'bleue' ^= 'bleu blanc rouge' ? 9 : 5; }";
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
"          INTEGER \"\" I:5\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==

"div{z-index:5}\n"
+ csspp_test::get_close_comment()

                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    // left side larger than right side
    CATCH_START_SECTION("compare 'bleu blanc rouge' ^= 'bleu' -- always false")
    {
        std::stringstream ss;
        ss << "div { z-index: 'bleu blanc rouge' ^= 'bleu' ? 9 : 5; }";
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
"          INTEGER \"\" I:5\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==

"div{z-index:5}\n"
+ csspp_test::get_close_comment()

                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("compare 'blue blanc rouge'.substr(...) $= 'bleu blanc rouge' -- always true")
    {
        std::string const str("bleu blanc rouge");
        for(size_t idx(0); idx < str.length(); ++idx)
        {
            std::stringstream ss;
            ss << "div { z-index: '"
               << str.substr(idx)
               << "' $= 'bleu blanc rouge' ? 9 : 5; }";
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
"          INTEGER \"\" I:9\n"
+ csspp_test::get_close_comment(true)

                );

            std::stringstream assembler_out;
            csspp::assembler a(assembler_out);
            a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

            CATCH_REQUIRE(assembler_out.str() ==

"div{z-index:9}\n"
+ csspp_test::get_close_comment()

                    );

            CATCH_REQUIRE(c.get_root() == n);
        }
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("compare 'blue blanc rouge'.substr(...) $= 'bleu blanc rouge' -- always false")
    {
        std::string const str("bleu blanc rouge");
        for(size_t idx(1); idx < str.length() - 1; ++idx)
        {
            std::stringstream ss;
            ss << "div { z-index: '"
               << str.substr(0, idx)
               << "' $= 'bleu blanc rouge' ? 9 : 5; }";
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
"          INTEGER \"\" I:5\n"
+ csspp_test::get_close_comment(true)

                );

            std::stringstream assembler_out;
            csspp::assembler a(assembler_out);
            a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

            CATCH_REQUIRE(assembler_out.str() ==

"div{z-index:5}\n"
+ csspp_test::get_close_comment()

                    );

            CATCH_REQUIRE(c.get_root() == n);
        }
    }
    CATCH_END_SECTION()

    // slight variation
    CATCH_START_SECTION("compare 'bouge' $= 'bleu blanc rouge' -- always false")
    {
        std::stringstream ss;
        ss << "div { z-index: 'bouge' $= 'bleu blanc rouge' ? 9 : 5; }";
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
"          INTEGER \"\" I:5\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==

"div{z-index:5}\n"
+ csspp_test::get_close_comment()

                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    // left larger than right
    CATCH_START_SECTION("compare 'bleu blanc rouge' $= 'rouge' -- always false")
    {
        std::stringstream ss;
        ss << "div { z-index: 'bleu blanc rouge' $= 'rouge' ? 9 : 5; }";
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
"          INTEGER \"\" I:5\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==

"div{z-index:5}\n"
+ csspp_test::get_close_comment()

                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("compare 'blue blanc rouge'.substr(...) *= 'bleu blanc rouge' -- always true")
    {
        std::string const str("bleu blanc rouge");
        for(size_t idx(0); idx < 10; ++idx)
        {
            size_t const start(rand() % (str.length() - 1));
            size_t len(rand() % (str.length() - start - 1) + 1);
            std::stringstream ss;
            ss << "div { z-index: '"
               << str.substr(start, len)
               << "' *= '"
               << str
               << "' ? 9 : 5; }";
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
"          INTEGER \"\" I:9\n"
+ csspp_test::get_close_comment(true)

                );

            std::stringstream assembler_out;
            csspp::assembler a(assembler_out);
            a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

            CATCH_REQUIRE(assembler_out.str() ==

"div{z-index:9}\n"
+ csspp_test::get_close_comment()

                    );

            CATCH_REQUIRE(c.get_root() == n);
        }
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("compare 'blue blanc rouge'.substr(...) *= 'bleu blanc rouge' -- always false")
    {
        std::string const str("bleu blanc rouge");
        for(size_t idx(0); idx < 10; ++idx)
        {
            size_t const start(rand() % (str.length() - 1));
            size_t len(rand() % (str.length() - start - 1) + 1);
            std::stringstream ss;
            ss << "div { z-index: '"
               << str.substr(start, len)
               << (rand() % 10 + '0') // make sure it cannot be found
               << "' *= '"
               << str
               << "' ? 9 : 5; }";
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
"          INTEGER \"\" I:5\n"
+ csspp_test::get_close_comment(true)

                );

            std::stringstream assembler_out;
            csspp::assembler a(assembler_out);
            a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

            CATCH_REQUIRE(assembler_out.str() ==

"div{z-index:5}\n"
+ csspp_test::get_close_comment()

                    );

            CATCH_REQUIRE(c.get_root() == n);
        }
    }
    CATCH_END_SECTION()

    // slight variation
    CATCH_START_SECTION("compare 'bouge' *= 'bleu blanc rouge' -- always false")
    {
        std::stringstream ss;
        ss << "div { z-index: 'bouge' *= 'bleu blanc rouge' ? 9 : 5; }";
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
"          INTEGER \"\" I:5\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==

"div{z-index:5}\n"
+ csspp_test::get_close_comment()

                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("compare 'bleu'|'blanc'|'rouge' |= 'bleu-blanc-rouge' -- always true")
    {
        for(size_t idx(0); idx < sizeof(colors) / sizeof(colors[0]); ++idx)
        {
            std::stringstream ss;
            ss << "div { z-index: '"
               << colors[idx]
               << "' *= 'bleu-blanc-rouge' ? 9 : 5; }";
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
"          INTEGER \"\" I:9\n"
+ csspp_test::get_close_comment(true)

                );

            std::stringstream assembler_out;
            csspp::assembler a(assembler_out);
            a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

            CATCH_REQUIRE(assembler_out.str() ==

"div{z-index:9}\n"
+ csspp_test::get_close_comment()

                    );

            CATCH_REQUIRE(c.get_root() == n);
        }
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("compare 'bleu#' |= 'bleu-blanc-rouge' -- always false")
    {
        for(size_t idx(0); idx < sizeof(colors) / sizeof(colors[0]); ++idx)
        {
            std::stringstream ss;
            ss << "div { z-index: '"
               << colors[idx]
               << (rand() % 10 + '0') // make sure it cannot be found
               << "' |= 'bleu-blanc-rouge' ? 9 : 5; }";
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
"          INTEGER \"\" I:5\n"
+ csspp_test::get_close_comment(true)

                );

            std::stringstream assembler_out;
            csspp::assembler a(assembler_out);
            a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

            CATCH_REQUIRE(assembler_out.str() ==

"div{z-index:5}\n"
+ csspp_test::get_close_comment()

                    );

            CATCH_REQUIRE(c.get_root() == n);
        }
    }
    CATCH_END_SECTION()

    // slight variation
    CATCH_START_SECTION("compare 'bouge' *= 'bleu-blanc-rouge' -- always false")
    {
        std::stringstream ss;
        ss << "div { z-index: 'bouge' *= 'bleu-blanc-rouge' ? 9 : 5; }";
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
"          INTEGER \"\" I:5\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==

"div{z-index:5}\n"
+ csspp_test::get_close_comment()

                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Expression number/string/invalid <,<=,>,>= number/string/invalid", "[expression] [equality] [invalid]")
{
    char const * op[] =
    {
        "=",
        "!=",
        "~=",
        "^=",
        "$=",
        "*=",
        "|="
    };

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

    CATCH_START_SECTION("number ?? ? is invalid")
    {
        for(size_t idx(0); idx < sizeof(op) / sizeof(op[0]); ++idx)
        {
            std::stringstream ss;
            ss << "div { width: 10px "
               << op[idx]
               << " ?; }";
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
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("number ~=,^=,$=,*=,|= number is invalid")
    {
        for(size_t idx(0); idx < sizeof(op) / sizeof(op[0]); ++idx)
        {
            if(idx >= 2)
            {
                // enhance by adding all other types (all types except STRING
                // are invalid with these operators)
                std::stringstream ss;
                ss << "div { width: 10px "
                   << op[idx]
                   << " 25px; }";
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

                VERIFY_ERRORS("test.css(1): error: incompatible types between INTEGER and INTEGER for operator '~=', '^=', '$=', '*=', '|='.\n");

                CATCH_REQUIRE(c.get_root() == n);
            }

            // mismatched dimensions are caught earlier with a different
            // error
            {
                // enhance by adding all other types (all types except STRING
                // are invalid with these operators)
                std::stringstream ss;
                ss << "div { width: 10px "
                   << op[idx]
                   << " 25em; }";
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

                VERIFY_ERRORS("test.css(1): error: incompatible types or dimensions between INTEGER and INTEGER for operator '=', '!=', '<', '<=', '>', '>=', '~=', '^=', '$=', '*=', or '|='.\n");

                CATCH_REQUIRE(c.get_root() == n);
            }
        }
    }
    CATCH_END_SECTION()

    // no error left over
    VERIFY_ERRORS("");
}

// vim: ts=4 sw=4 et
