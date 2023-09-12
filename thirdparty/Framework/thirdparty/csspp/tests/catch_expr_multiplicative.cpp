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
 * \brief Test the expression.cpp file: "*", "/", and "%" operators.
 *
 * This test runs a battery of tests agains the expression.cpp "*", "/",
 * and "%" operators to ensure full coverage and that all possible left
 * hand side and right hand side types are checked for the multiplicative
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



CATCH_TEST_CASE("Expression integer *,/,% integer", "[expression] [multiplicative]")
{
    CATCH_START_SECTION("multiple sizes without dimensions (*)")
    {
        std::stringstream ss;
        ss << "div { z-index: 3 * 10; }";
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
"          INTEGER \"\" I:30\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{z-index:30}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("multiple sizes without dimensions (mul)")
    {
        std::stringstream ss;
        ss << "div { z-index: 3 mul 10; }";
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
"          INTEGER \"\" I:30\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{z-index:30}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("divide sizes without dimensions (/)")
    {
        std::stringstream ss;
        ss << "div { z-index: 10 / 3; }";
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
"          INTEGER \"\" I:3\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{z-index:3}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("divide sizes without dimensions (div)")
    {
        std::stringstream ss;
        ss << "div { z-index: 10 div 3; }";
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
"          INTEGER \"\" I:3\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{z-index:3}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("modulo sizes without dimensions (%)")
    {
        std::stringstream ss;
        ss << "div { z-index: 10 % 3; }";
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
"          INTEGER \"\" I:1\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{z-index:1}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("modulo sizes without dimensions (mod)")
    {
        std::stringstream ss;
        ss << "div { z-index: 10 mod 3; }";
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
"          INTEGER \"\" I:1\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{z-index:1}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("remove dimension (simple divide)")
    {
        std::stringstream ss;
        ss << "div { width: 3px / 1px; }";
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
"          INTEGER \"\" I:3\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream out;
        csspp::assembler a(out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(out.str() ==
"div{width:3}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("convert px to em (multiple + divide)")
    {
        std::stringstream ss;
        ss << "div { width: 3px * 10em / 1px; }";
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
"          INTEGER \"em\" I:30\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream out;
        csspp::assembler a(out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(out.str() ==
"div{width:30em}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("operation going through a unit less dividend")
    {
        std::stringstream ss;
        ss << "div { width: 30px / 1px / 10 * 10cm; }";
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
"          INTEGER \"cm\" I:30\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream out;
        csspp::assembler a(out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(out.str() ==
"div{width:30cm}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("complex operations with unitless dividend")
    {
        std::stringstream ss;
        ss << "div { width: 3px * 10em / 1px / 5em / 2vw * 3em * 5vw; }";
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
"          INTEGER \"em\" I:45\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream out;
        csspp::assembler a(out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(out.str() ==
"div{width:45em}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("many divisions with many dimension, then multiplications")
    {
        std::stringstream ss;
        ss << "div { width: 60 / 5px / 3em / 2vw * 3em * 5vw * 2px * 3px; }";
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
"          INTEGER \"px\" I:180\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream out;
        csspp::assembler a(out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(out.str() ==
"div{width:180px}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("many multiplications and divisions with differen dimensions")
    {
        std::stringstream ss;
        // first we get "deg * px * em * vw / pt * rad * cm * mm" then
        // we clean that up and get 1
        ss << "div { width: (60deg * 5px * 3em * 2vw / 3pt / 5rad / 2cm / 3mm) / (60deg * 5px * 3em * 2vw / 3pt / 5rad / 2cm / 3mm); }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

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
"          INTEGER \"\" I:1\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream out;
        csspp::assembler a(out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(out.str() ==
"div{width:1}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("modulo of a distance, require same dimension")
    {
        std::stringstream ss;
        ss << "div { width: 10px % 3px; }";
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
"          INTEGER \"px\" I:1\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream out;
        csspp::assembler a(out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(out.str() ==
"div{width:1px}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Expression integer *,/,% integer with incompatible dimensions", "[expression] [multiplicative] [invalid]")
{
    // px * px -- cannot output
    {
        std::stringstream ss;
        ss << "div { width: 3px * 10px; }";
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
"          INTEGER \"px * px\" I:30\n"
+ csspp_test::get_close_comment(true)

            );

        // no errors so far
        VERIFY_ERRORS("");

        std::stringstream out;
        csspp::assembler a(out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is [" << out.str() << "]\n";

        VERIFY_ERRORS("test.css(1): error: \"px * px\" is not a valid CSS dimension.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // px / em -- cannot output
    {
        std::stringstream ss;
        ss << "div { width: 10px / 3em; }";
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
"          INTEGER \"px / em\" I:3\n"
+ csspp_test::get_close_comment(true)

            );

        // no errors so far
        VERIFY_ERRORS("");

        std::stringstream out;
        csspp::assembler a(out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is [" << out.str() << "]\n";

        VERIFY_ERRORS("test.css(1): error: \"px / em\" is not a valid CSS dimension.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // px % em -- cannot calculate
    {
        std::stringstream ss;
        ss << "div { width: 10px % 3em; }";
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

        VERIFY_ERRORS("test.css(1): error: incompatible dimensions (\"px\" and \"em\") cannot be used with operator '%'.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // string * -integer
    {
        std::stringstream ss;
        ss << "div { width: \"lhs\" * -2; }";
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

        VERIFY_ERRORS("test.css(1): error: string * integer requires that the integer not be negative (-2).\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // string / integer
    {
        std::stringstream ss;
        ss << "div { width: \"lhs\" / 3; }";
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

        VERIFY_ERRORS("test.css(1): error: incompatible types between STRING and INTEGER for operator '/' or '%'.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // string % integer
    {
        std::stringstream ss;
        ss << "div { width: \"lhs\" % 5; }";
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

        VERIFY_ERRORS("test.css(1): error: incompatible types between STRING and INTEGER for operator '/' or '%'.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // string * decimal-number
    {
        std::stringstream ss;
        ss << "div { width: \"lhs\" * 3.14; }";
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

        VERIFY_ERRORS("test.css(1): error: incompatible types between STRING and DECIMAL_NUMBER for operator '*', '/', or '%'.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // string * percent
    {
        std::stringstream ss;
        ss << "div { width: \"lhs\" / 31%; }";
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

        VERIFY_ERRORS("test.css(1): error: incompatible types between STRING and PERCENT for operator '*', '/', or '%'.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    CATCH_START_SECTION("unicode-range % string")
    {
        std::stringstream ss;
        ss << "div { width: U+5?? % \"rhs\"; }";
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

        VERIFY_ERRORS("test.css(1): error: incompatible types between UNICODE_RANGE and STRING for operator '*', '/', or '%'.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    // string * string
    {
        std::stringstream ss;
        ss << "div { width: \"lhs\" * 'rhs'; }";
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

        VERIFY_ERRORS("test.css(1): error: incompatible types between STRING and STRING for operator '*', '/', or '%'.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // string / string
    {
        std::stringstream ss;
        ss << "div { width: \"lhs\" / 'rhs'; }";
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

        VERIFY_ERRORS("test.css(1): error: incompatible types between STRING and STRING for operator '*', '/', or '%'.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // string % string
    {
        std::stringstream ss;
        ss << "div { width: \"lhs\" % 'rhs'; }";
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

        VERIFY_ERRORS("test.css(1): error: incompatible types between STRING and STRING for operator '*', '/', or '%'.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Expression string * integer", "[expression] [multiplicative] [string]")
{
    // duplicate a string (string x integer)
    {
        std::stringstream ss;
        ss << "div::before { content: \"str\" * 3; }";
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
"      COLON\n"
"      COLON\n"
"      IDENTIFIER \"before\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"content\"\n"
"        ARG\n"
"          STRING \"strstrstr\"\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div::before{content:\"strstrstr\"}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // duplicate a string (integer x string)
    {
        std::stringstream ss;
        ss << "div::before { content: 3 * \"str\"; }";
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
"      COLON\n"
"      COLON\n"
"      IDENTIFIER \"before\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"content\"\n"
"        ARG\n"
"          STRING \"strstrstr\"\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div::before{content:\"strstrstr\"}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // duplicate a string (string x 0)
    {
        std::stringstream ss;
        ss << "div::before { content: \"str\" * 0; }";
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
"      COLON\n"
"      COLON\n"
"      IDENTIFIER \"before\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"content\"\n"
"        ARG\n"
"          STRING \"\"\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div::before{content:\"\"}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // duplicate a string (0 x string)
    {
        std::stringstream ss;
        ss << "div::before { content: 0 * \"str\"; }";
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
"      COLON\n"
"      COLON\n"
"      IDENTIFIER \"before\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"content\"\n"
"        ARG\n"
"          STRING \"\"\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div::before{content:\"\"}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Expression multiplicative errors", "[expression] [multiplicative] [invalid]")
{
    CATCH_START_SECTION("? is not a valid unary")
    {
        std::stringstream ss;
        ss << "div { width: ?; }";
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

    CATCH_START_SECTION("5 * ? is not valid")
    {
        std::stringstream ss;
        ss << "div { width: 5 * ?; }";
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

    CATCH_START_SECTION("3 / ? is not valid")
    {
        std::stringstream ss;
        ss << "div { width: 3 / ?; }";
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

    CATCH_START_SECTION("3 % ? is not valid")
    {
        std::stringstream ss;
        ss << "div { width: 3 % ?; }";
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

    CATCH_START_SECTION("3 / 0 is not accepted")
    {
        std::stringstream ss;
        ss << "div { width: 3 / 0; }";
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

        VERIFY_ERRORS("test.css(1): error: division by zero.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("3.3 / 0.0 is not accepted")
    {
        std::stringstream ss;
        ss << "div { width: 3.3 / 0.0; }";
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

        VERIFY_ERRORS("test.css(1): error: division by zero.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("3 % 0 is not accepted")
    {
        std::stringstream ss;
        ss << "div { width: 3 % 0; }";
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

        VERIFY_ERRORS("test.css(1): error: modulo by zero.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("3.9 % 0.0 is not accepted")
    {
        std::stringstream ss;
        ss << "div { width: 3.9 % 0.0; }";
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

        VERIFY_ERRORS("test.css(1): error: modulo by zero.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Expression decimal number or integer *,/,% decimal number or integer", "[expression] [multiplicative]")
{
    CATCH_START_SECTION("multiply two decimal numbers")
    {
        std::stringstream ss;
        ss << "div { z-index: 3.5 * 10.2; }";
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

        VERIFY_ERRORS("");

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
"          DECIMAL_NUMBER \"\" D:35.7\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{z-index:35.7}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("multiply an integer with a decimal number")
    {
        std::stringstream ss;
        ss << "div { z-index: 3 * 10.2; }";
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

        VERIFY_ERRORS("");

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
"          DECIMAL_NUMBER \"\" D:30.6\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{z-index:30.6}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("multiply a decimal number with an integer")
    {
        std::stringstream ss;
        ss << "div { z-index: 3.5 * 10; }";
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

        VERIFY_ERRORS("");

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
"          DECIMAL_NUMBER \"\" D:35\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{z-index:35}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("multiply decimal numbers with 0 in their fraction")
    {
        std::stringstream ss;
        ss << "div { z-index: 3.0 * 10.0; }";
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

        VERIFY_ERRORS("");

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
"          DECIMAL_NUMBER \"\" D:30\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{z-index:30}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("multiply an integer and a decimal number with 0 in their fraction")
    {
        std::stringstream ss;
        ss << "div { z-index: 3 * 10.0; }";
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

        VERIFY_ERRORS("");

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
"          DECIMAL_NUMBER \"\" D:30\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{z-index:30}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("multiply a decimal number with 0 in their fraction and an integer")
    {
        std::stringstream ss;
        ss << "div { z-index: 3.0 * 10; }";
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

        VERIFY_ERRORS("");

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
"          DECIMAL_NUMBER \"\" D:30\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{z-index:30}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("divide two decimal numbers")
    {
        std::stringstream ss;
        ss << "div { z-index: 3.2 / 12.5; }";
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

        VERIFY_ERRORS("");

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
"          DECIMAL_NUMBER \"\" D:0.256\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{z-index:.256}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("divide an integer with a decimal number")
    {
        std::stringstream ss;
        ss << "div { z-index: 3 / 12.5; }";
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

        VERIFY_ERRORS("");

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
"          DECIMAL_NUMBER \"\" D:0.24\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{z-index:.24}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("divide a decimal number with an integer")
    {
        std::stringstream ss;
        ss << "div { z-index: 3.5 / 10; }";
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

        VERIFY_ERRORS("");

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
"          DECIMAL_NUMBER \"\" D:0.35\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{z-index:.35}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("divide decimal numbers with 0 in their fraction")
    {
        std::stringstream ss;
        ss << "div { z-index: 3.0 / 10.0; }";
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

        VERIFY_ERRORS("");

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
"          DECIMAL_NUMBER \"\" D:0.3\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{z-index:.3}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("divide an integer and a decimal number with 0 in their fraction")
    {
        std::stringstream ss;
        ss << "div { z-index: 350 / 10.0; }";
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

        VERIFY_ERRORS("");

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
"          DECIMAL_NUMBER \"\" D:35\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{z-index:35}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("divide a decimal number with 0 in their fraction and an integer")
    {
        std::stringstream ss;
        ss << "div { z-index: 30.0 / 10; }";
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

        VERIFY_ERRORS("");

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
"          DECIMAL_NUMBER \"\" D:3\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{z-index:3}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("modulo two decimal numbers")
    {
        std::stringstream ss;
        ss << "div { z-index: 3.5 % 10.2; }";
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

        VERIFY_ERRORS("");

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
"          DECIMAL_NUMBER \"\" D:3.5\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{z-index:3.5}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("modulo an integer with a decimal number")
    {
        std::stringstream ss;
        ss << "div { z-index: 33 % 10.2; }";
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

        VERIFY_ERRORS("");

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
"          DECIMAL_NUMBER \"\" D:2.4\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{z-index:2.4}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("modulo a decimal number with an integer")
    {
        std::stringstream ss;
        ss << "div { z-index: 3.5 % 10; }";
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

        VERIFY_ERRORS("");

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
"          DECIMAL_NUMBER \"\" D:3.5\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{z-index:3.5}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("modulo decimal numbers with 0 in their fraction")
    {
        std::stringstream ss;
        ss << "div { z-index: 3.0 % 10.0; }";
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

        VERIFY_ERRORS("");

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
"          DECIMAL_NUMBER \"\" D:3\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{z-index:3}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("modulo an integer and a decimal number with 0 in their fraction")
    {
        std::stringstream ss;
        ss << "div { z-index: 3 % 10.0; }";
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

        VERIFY_ERRORS("");

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
"          DECIMAL_NUMBER \"\" D:3\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{z-index:3}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("modulo a decimal number with 0 in their fraction and an integer")
    {
        std::stringstream ss;
        ss << "div { z-index: 3.0 % 10; }";
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

        VERIFY_ERRORS("");

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
"          DECIMAL_NUMBER \"\" D:3\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{z-index:3}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Expression number *,/,% number with hand-made dimensions", "[expression] [multiplicative] [dimension]")
{
    CATCH_START_SECTION("px * px / px")
    {
        std::stringstream ss;
        ss << "p.edged { border: { width: 25px\\ \\*\\ px / 1px; }; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

        VERIFY_ERRORS("");

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
"      IDENTIFIER \"p\"\n"
"      PERIOD\n"
"      IDENTIFIER \"edged\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"border-width\"\n"
"        ARG\n"
"          INTEGER \"px\" I:25\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"p.edged{border-width:25px}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("px*px/px (i.e. not spaces this time)")
    {
        std::stringstream ss;
        ss << "p.edged{border:{width:21px\\*px/7px};}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

        VERIFY_ERRORS("");

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
"      IDENTIFIER \"p\"\n"
"      PERIOD\n"
"      IDENTIFIER \"edged\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"border-width\"\n"
"        ARG\n"
"          INTEGER \"px\" I:3\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"p.edged{border-width:3px}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("px*px/px (i.e. not spaces this time)")
    {
        std::stringstream ss;
        ss << "p.edged{border:{width:21px\\*px/7px};}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

        VERIFY_ERRORS("");

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
"      IDENTIFIER \"p\"\n"
"      PERIOD\n"
"      IDENTIFIER \"edged\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"border-width\"\n"
"        ARG\n"
"          INTEGER \"px\" I:3\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"p.edged{border-width:3px}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("em *px / px (missing one space")
    {
        std::stringstream ss;
        ss << "p.edged{border:{width:28em\\ \\*px/7px};}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

        VERIFY_ERRORS("");

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
"      IDENTIFIER \"p\"\n"
"      PERIOD\n"
"      IDENTIFIER \"edged\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"border-width\"\n"
"        ARG\n"
"          INTEGER \"em\" I:4\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"p.edged{border-width:4em}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("em* px / px (missing the other space)")
    {
        std::stringstream ss;
        ss << "p.edged{border:{width:28em\\*\\ px/7px};}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

        VERIFY_ERRORS("");

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
"      IDENTIFIER \"p\"\n"
"      PERIOD\n"
"      IDENTIFIER \"edged\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"border-width\"\n"
"        ARG\n"
"          INTEGER \"em\" I:4\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"p.edged{border-width:4em}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("one space after the dimension is ignored")
    {
        std::stringstream ss;
        ss << "p.edged{border:{width:28em\\ *1px};}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

        VERIFY_ERRORS("");

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
"      IDENTIFIER \"p\"\n"
"      PERIOD\n"
"      IDENTIFIER \"edged\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"border-width\"\n"
"        ARG\n"
"          INTEGER \"em * px\" I:28\n"
+ csspp_test::get_close_comment(true)

            );

// Output would fail because of the double dimension...
//         std::stringstream assembler_out;
//         csspp::assembler a(assembler_out);
//         a.output(n, csspp::output_mode_t::COMPRESSED);
// 
// //std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";
// 
//         CATCH_REQUIRE(assembler_out.str() ==
// "p.edged{border-width:4em}\n"
// + csspp_test::get_close_comment()
//                 );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("\"1 / px\" test")
    {
        std::stringstream ss;
        // IMPORTANT NOTE: to start the dimension with "1" we need to
        //                 use escape character '\\31'
        ss << "p .edged { border : { width : 28\\31 \\/\\ em * 3em * 5px; } ; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

        VERIFY_ERRORS("");

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
"      IDENTIFIER \"p\"\n"
"      WHITESPACE\n"
"      PERIOD\n"
"      IDENTIFIER \"edged\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"border-width\"\n"
"        ARG\n"
"          INTEGER \"px\" I:420\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"p .edged{border-width:420px}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    // make sure we really had no errors
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Expression number *,/,% number with invalid hand-made dimensions", "[expression] [multiplicative] [invalid] [dimension]")
{
    CATCH_START_SECTION("\"25px *\" -- missing second dimension")
    {
        std::stringstream ss;
        ss << "p.edged { width: 25px\\ \\* * 3px; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

        VERIFY_ERRORS("test.css(1): error: number dimension is missing a dimension name.\n");

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("\"25px*\" -- missing second dimension (no space)")
    {
        std::stringstream ss;
        ss << "p.edged { width: 25px\\* * 3px; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

        VERIFY_ERRORS("test.css(1): error: number dimension is missing a dimension name.\n");

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("\"25\\ *\\ px\" -- missing first dimension")
    {
        std::stringstream ss;
        ss << "p.edged { width: 25\\ \\*\\ px * 3px; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

        VERIFY_ERRORS("test.css(1): error: number dimension is missing a dimension name.\n");

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("\"25\\*px\" -- missing first dimension (no space)")
    {
        std::stringstream ss;
        ss << "p.edged { width: 25\\*px * 3px; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

        VERIFY_ERRORS("test.css(1): error: number dimension is missing a dimension name.\n");

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("\"px / em / pt\" -- two slashes is not valid")
    {
        std::stringstream ss;
        ss << "p.edged { width: 25px\\ \\/\\ em\\ \\/\\ pt * 3px; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

        VERIFY_ERRORS("test.css(1): error: a valid dimension can have any number of '*' operators and a single '/' operator, here we found a second '/'.\n");

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("\"1 / em / pt\" -- two slashes is not valid")
    {
        std::stringstream ss;
        // IMPORTANT NOTE: to start the dimension with "1" we need to
        //                 use escape character '\\31'
        ss << "p.edged { width: 25\\31\\ \\/\\ em\\ \\/\\ pt * 3px; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

        VERIFY_ERRORS("test.css(1): error: a valid dimension can have any number of '*' operators and a single '/' operator, here we found a second '/'.\n");

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("\"1/em/pt\" -- two slashes is not valid (no spaces)")
    {
        std::stringstream ss;
        // IMPORTANT NOTE: to start the dimension with "1" we need to
        //                 use escape character '\\31'
        ss << "p.edged { width: 25\\31\\/em\\/pt * 3px; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

        VERIFY_ERRORS("test.css(1): error: a valid dimension can have any number of '*' operators and a single '/' operator, here we found a second '/'.\n");

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("\"em % pt\" -- '%' is not a valid dimension separator")
    {
        std::stringstream ss;
        ss << "p.edged { width: 25em\\ \\%\\ pt / 5pt; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

        VERIFY_ERRORS("test.css(1): error: multiple dimensions can only be separated by '*' or '/' not '%'.\n");

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    // make sure we really had no errors
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Expression percent *,/,% percent", "[expression] [multiplicative]")
{
    CATCH_START_SECTION("percent multiplication")
    {
        std::stringstream ss;
        ss << "div { height: 3.5% * 10.2%; }";
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

        VERIFY_ERRORS("");

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
"      DECLARATION \"height\"\n"
"        ARG\n"
"          PERCENT D:0.004\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{height:.357%}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("percent multiplication with what looks like an integer (lhs)")
    {
        std::stringstream ss;
        ss << "div { height: 3% * 10.2%; }";
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

        VERIFY_ERRORS("");

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
"      DECLARATION \"height\"\n"
"        ARG\n"
"          PERCENT D:0.003\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{height:.306%}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("percent multiplication with what looks like an integer (rhs)")
    {
        std::stringstream ss;
        ss << "div { height: 3.5% * 10%; }";
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

        VERIFY_ERRORS("");

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
"      DECLARATION \"height\"\n"
"        ARG\n"
"          PERCENT D:0.004\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{height:.35%}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("percent division")
    {
        std::stringstream ss;
        ss << "div { height: 3.5% / 12.5%; }";
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

        VERIFY_ERRORS("");

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
"      DECLARATION \"height\"\n"
"        ARG\n"
"          PERCENT D:0.28\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{height:28%}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("percent division with what looks like an integer (lhs)")
    {
        std::stringstream ss;
        ss << "div { height: 3% / 12.5%; }";
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

        VERIFY_ERRORS("");

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
"      DECLARATION \"height\"\n"
"        ARG\n"
"          PERCENT D:0.24\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{height:24%}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("percent division with what looks like an integer (rhs)")
    {
        std::stringstream ss;
        ss << "div { height: 3.5% / 10%; }";
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

        VERIFY_ERRORS("");

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
"      DECLARATION \"height\"\n"
"        ARG\n"
"          PERCENT D:0.35\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{height:35%}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("percent modulo")
    {
        std::stringstream ss;
        ss << "div { height: 13.5% mod 12.5%; }";
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

        VERIFY_ERRORS("");

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
"      DECLARATION \"height\"\n"
"        ARG\n"
"          PERCENT D:0.01\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{height:1%}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("percent modulo with what looks like an integer (lhs)")
    {
        std::stringstream ss;
        ss << "div { height: 23% mod 12.5%; }";
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

        VERIFY_ERRORS("");

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
"      DECLARATION \"height\"\n"
"        ARG\n"
"          PERCENT D:0.105\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{height:10.5%}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("percent modulo with what looks like an integer (rhs)")
    {
        std::stringstream ss;
        ss << "div { height: 3.5% mod 10%; }";
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

        VERIFY_ERRORS("");

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
"      DECLARATION \"height\"\n"
"        ARG\n"
"          PERCENT D:0.035\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{height:3.5%}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("percent and decimal number multiplication")
    {
        std::stringstream ss;
        ss << "div { height: 3.5% * 10.2px; }";
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

        VERIFY_ERRORS("");

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
"      DECLARATION \"height\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"px\" D:0.357\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{height:.357px}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("percent multiplication with what looks like an integer (lhs)")
    {
        std::stringstream ss;
        ss << "div { height: 3% * 10.2em; }";
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

        VERIFY_ERRORS("");

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
"      DECLARATION \"height\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"em\" D:0.306\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{height:.306em}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("percent multiplication with what looks like an integer (rhs)")
    {
        std::stringstream ss;
        ss << "div { height: 3.5% * 10cm; }";
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

        VERIFY_ERRORS("");

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
"      DECLARATION \"height\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"cm\" D:0.35\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{height:.35cm}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("percent and decimal number division")
    {
        std::stringstream ss;
        ss << "div { height: 70.0vw / 3.5%; }";
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

        VERIFY_ERRORS("");

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
"      DECLARATION \"height\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"vw\" D:2000\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{height:2000vw}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("percent division with what looks like an integer (lhs)")
    {
        std::stringstream ss;
        ss << "div { height: 3px / 12.5%; }";
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

        VERIFY_ERRORS("");

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
"      DECLARATION \"height\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"px\" D:24\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{height:24px}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("percent division with what looks like an integer (rhs)")
    {
        std::stringstream ss;
        ss << "div { height: 3.5em / 10%; }";
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

        VERIFY_ERRORS("");

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
"      DECLARATION \"height\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"em\" D:35\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{height:35em}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Expression with multiplicative unicode ranges", "[expression] [multiplicative] [unicode-range-value]")
{
    CATCH_START_SECTION("null * null = null")
    {
        std::stringstream ss;
        ss << "@font-face { unicode-range: null * null; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

        VERIFY_ERRORS("");

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        // to verify that the result is still an INTEGER we have to
        // test the root node here
        std::stringstream compiler_out;
        compiler_out << *n;
        VERIFY_TREES(compiler_out.str(),

"LIST\n"
+ csspp_test::get_default_variables() +
"  AT_KEYWORD \"font-face\" I:0\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"unicode-range\"\n"
"        ARG\n"
"          NULL_TOKEN\n"
+ csspp_test::get_close_comment(true)

            );

// Assembler does not support NULL_TOKEN in its output
//         std::stringstream assembler_out;
//         csspp::assembler a(assembler_out);
//         a.output(n, csspp::output_mode_t::COMPRESSED);
// 
// //std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";
// 
//         CATCH_REQUIRE(assembler_out.str() ==
// "div{font:35pt/40pt serif}\n"
// + csspp_test::get_close_comment()
//                 );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("unicode * null = null")
    {
        std::stringstream ss;
        ss << "@font-face { unicode-range: U+7?? * null; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

        VERIFY_ERRORS("");

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        // to verify that the result is still an INTEGER we have to
        // test the root node here
        std::stringstream compiler_out;
        compiler_out << *n;
        VERIFY_TREES(compiler_out.str(),

"LIST\n"
+ csspp_test::get_default_variables() +
"  AT_KEYWORD \"font-face\" I:0\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"unicode-range\"\n"
"        ARG\n"
"          NULL_TOKEN\n"
+ csspp_test::get_close_comment(true)

            );

// Assembler does not support NULL_TOKEN in its output
//         std::stringstream assembler_out;
//         csspp::assembler a(assembler_out);
//         a.output(n, csspp::output_mode_t::COMPRESSED);
// 
// //std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";
// 
//         CATCH_REQUIRE(assembler_out.str() ==
// "div{font:35pt/40pt serif}\n"
// + csspp_test::get_close_comment()
//                 );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("null * unicode = null")
    {
        std::stringstream ss;
        ss << "@font-face { unicode-range: null * U+7??; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

        VERIFY_ERRORS("");

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        // to verify that the result is still an INTEGER we have to
        // test the root node here
        std::stringstream compiler_out;
        compiler_out << *n;
        VERIFY_TREES(compiler_out.str(),

"LIST\n"
+ csspp_test::get_default_variables() +
"  AT_KEYWORD \"font-face\" I:0\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"unicode-range\"\n"
"        ARG\n"
"          NULL_TOKEN\n"
+ csspp_test::get_close_comment(true)

            );

// Assembler does not support NULL_TOKEN in its output
//         std::stringstream assembler_out;
//         csspp::assembler a(assembler_out);
//         a.output(n, csspp::output_mode_t::COMPRESSED);
// 
// //std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";
// 
//         CATCH_REQUIRE(assembler_out.str() ==
// "div{font:35pt/40pt serif}\n"
// + csspp_test::get_close_comment()
//                 );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("unicode * unicode = unicode (smaller range included in other range)")
    {
        std::stringstream ss;
        ss << "@font-face { unicode-range: U+1??? * U+17??; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

        VERIFY_ERRORS("");

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        // to verify that the result is still an INTEGER we have to
        // test the root node here
        std::stringstream compiler_out;
        compiler_out << *n;
        VERIFY_TREES(compiler_out.str(),

"LIST\n"
+ csspp_test::get_default_variables() +
"  AT_KEYWORD \"font-face\" I:0\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"unicode-range\"\n"
"        ARG\n"
"          UNICODE_RANGE I:26383984105216\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"@font-face {unicode-range:U+17??}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("unicode * unicode = null (no overlap)")
    {
        std::stringstream ss;
        ss << "@font-face { unicode-range: U+1??? * U+7??; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

        VERIFY_ERRORS("");

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        // to verify that the result is still an INTEGER we have to
        // test the root node here
        std::stringstream compiler_out;
        compiler_out << *n;
        VERIFY_TREES(compiler_out.str(),

"LIST\n"
+ csspp_test::get_default_variables() +
"  AT_KEYWORD \"font-face\" I:0\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"unicode-range\"\n"
"        ARG\n"
"          NULL_TOKEN\n"
+ csspp_test::get_close_comment(true)

            );

// Assembler does not support NULL_TOKEN in its output
//         std::stringstream assembler_out;
//         csspp::assembler a(assembler_out);
//         a.output(n, csspp::output_mode_t::COMPRESSED);
// 
// //std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";
// 
//         CATCH_REQUIRE(assembler_out.str() ==
// "div{font:35pt/40pt serif}\n"
// + csspp_test::get_close_comment()
//                 );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("unicode * unicode = null (start/end overlap)")
    {
        std::stringstream ss;
        ss << "@font-face { unicode-range: U+1000-18FF * U+1750-1FFF; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

        VERIFY_ERRORS("");

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

        // to verify that the result is still an INTEGER we have to
        // test the root node here
        std::stringstream compiler_out;
        compiler_out << *n;
        VERIFY_TREES(compiler_out.str(),

"LIST\n"
+ csspp_test::get_default_variables() +
"  AT_KEYWORD \"font-face\" I:0\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"unicode-range\"\n"
"        ARG\n"
"          UNICODE_RANGE I:27483495733072\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"@font-face {unicode-range:U+1750-18ff}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Expression with invalid multiplicative unicode ranges", "[expression] [multiplicative] [unicode-range-value] [invalid]")
{
    CATCH_START_SECTION("null / null = null")
    {
        std::stringstream ss;
        ss << "@font-face { unicode-range: null / null; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

        VERIFY_ERRORS("test.css(1): error: unicode_range * unicode_range is the only multiplicative operator accepted with unicode ranges, '/' and '%' are not allowed.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("unicode % null = null")
    {
        std::stringstream ss;
        ss << "@font-face { unicode-range: U+7?? % null; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

        VERIFY_ERRORS("test.css(1): error: unicode_range * unicode_range is the only multiplicative operator accepted with unicode ranges, '/' and '%' are not allowed.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("null / unicode = null")
    {
        std::stringstream ss;
        ss << "@font-face { unicode-range: null / U+7??; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

        VERIFY_ERRORS("test.css(1): error: unicode_range * unicode_range is the only multiplicative operator accepted with unicode ranges, '/' and '%' are not allowed.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("unicode % unicode is an error")
    {
        std::stringstream ss;
        ss << "@font-face { unicode-range: U+1??? % U+17??; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

        VERIFY_ERRORS("test.css(1): error: unicode_range * unicode_range is the only multiplicative operator accepted with unicode ranges, '/' and '%' are not allowed.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("unicode / unicode = null (no overlap)")
    {
        std::stringstream ss;
        ss << "@font-face { unicode-range: U+1??? / U+7??; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

        VERIFY_ERRORS("test.css(1): error: unicode_range * unicode_range is the only multiplicative operator accepted with unicode ranges, '/' and '%' are not allowed.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("unicode % unicode = error really")
    {
        std::stringstream ss;
        ss << "@font-face { unicode-range: U+1000-18FF % U+1750-1FFF; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

        VERIFY_ERRORS("test.css(1): error: unicode_range * unicode_range is the only multiplicative operator accepted with unicode ranges, '/' and '%' are not allowed.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Expression with a font metrics", "[expression] [multiplicative] [font-metrics]")
{
    CATCH_START_SECTION("Not a division, two integers")
    {
        std::stringstream ss;
        ss << "div { font: 35pt/40pt serif; }";
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

        VERIFY_ERRORS("");

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
"      DECLARATION \"font\"\n"
"        ARG\n"
"          FONT_METRICS FM:35pt/40pt\n"
"          WHITESPACE\n"
"          IDENTIFIER \"serif\"\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{font:35pt/40pt serif}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("Not a division, integer and percent")
    {
        std::stringstream ss;
        ss << "div { font: 35pt/120% sans-serif; }";
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

        VERIFY_ERRORS("");

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
"      DECLARATION \"font\"\n"
"        ARG\n"
"          FONT_METRICS FM:35pt/120%\n"
"          WHITESPACE\n"
"          IDENTIFIER \"sans-serif\"\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{font:35pt/120% sans-serif}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("Not a division, spaces and percent twice")
    {
        std::stringstream ss;
        ss << "div { font: 80% / 120% sans-serif; }";
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

        VERIFY_ERRORS("");

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
"      DECLARATION \"font\"\n"
"        ARG\n"
"          FONT_METRICS FM:80%/120%\n"
"          WHITESPACE\n"
"          IDENTIFIER \"sans-serif\"\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{font:80%/120% sans-serif}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Expression with colors", "[expression] [multiplicative] [colors]")
{
    CATCH_START_SECTION("Multiply color by 5")
    {
        std::stringstream ss;
        ss << "div {"
           << "  color: red * 5;"
           << "  border-top-left-color: 5 * teal;"
           << "  background-color: black * 5;"
           << "  border-bottom-left-color: 5 * azure;"
           << "  border-top-right-color: #123456 * 5;"
           << "}";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

//std::cerr << "parse [" << ss.str() << "]\n";
        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

        VERIFY_ERRORS("");

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
"      LIST\n"
"        DECLARATION \"color\"\n"
"          ARG\n"
"            COLOR H:ff0000ff\n"
"        DECLARATION \"border-top-left-color\"\n"
"          ARG\n"
"            COLOR H:ffffff00\n"
"        DECLARATION \"background-color\"\n"
"          ARG\n"
"            COLOR H:ff000000\n"
"        DECLARATION \"border-bottom-left-color\"\n"
"          ARG\n"
"            COLOR H:ffffffff\n"
"        DECLARATION \"border-top-right-color\"\n"
"          ARG\n"
"            COLOR H:ffffff5a\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{color:red;border-top-left-color:aqua;background-color:#000;border-bottom-left-color:#fff;border-top-right-color:#5affff}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("Divide color by 5")
    {
        // note: we do not check the swapped version here, that's done in the
        //       test checking for invalid operations
        std::stringstream ss;
        ss << "div {"
           << "  color: red / 5;"
           << "  background-color: black / 5;"
           << "  border-top-right-color: #123456 / 5;"
           << "}";
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

        VERIFY_ERRORS("");

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
"      LIST\n"
"        DECLARATION \"color\"\n"
"          ARG\n"
"            COLOR H:33000033\n"
"        DECLARATION \"background-color\"\n"
"          ARG\n"
"            COLOR H:33000000\n"
"        DECLARATION \"border-top-right-color\"\n"
"          ARG\n"
"            COLOR H:33110a04\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{color:rgba(51,0,0,.2);background-color:rgba(0,0,0,.2);border-top-right-color:rgba(4,10,17,.2)}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("Modulo color by 55")
    {
        std::stringstream ss;
        ss << "div {"
           << "  color: red % .215686275;"
           << "  background-color: black % .21568627555;"
           << "  border-top-right-color: #123456 % .21568627555;"
           << "}";
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

        VERIFY_ERRORS("");

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
"      LIST\n"
"        DECLARATION \"color\"\n"
"          ARG\n"
"            COLOR H:23000023\n"
"        DECLARATION \"background-color\"\n"
"          ARG\n"
"            COLOR H:23000000\n"
"        DECLARATION \"border-top-right-color\"\n"
"          ARG\n"
"            COLOR H:231f3412\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{color:rgba(35,0,0,.14);background-color:rgba(0,0,0,.14);border-top-right-color:rgba(18,52,31,.14)}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("Multiply color by 1.5")
    {
        std::stringstream ss;
        ss << "div { color: red * 1.5; background-color: black * 1.5; border-color: #123456 * 1.5 }";
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

        VERIFY_ERRORS("");

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
"      LIST\n"
"        DECLARATION \"color\"\n"
"          ARG\n"
"            COLOR H:ff0000ff\n"
"        DECLARATION \"background-color\"\n"
"          ARG\n"
"            COLOR H:ff000000\n"
"        DECLARATION \"border-color\"\n"
"          ARG\n"
"            COLOR H:ff814e1b\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{color:red;background-color:#000;border-color:#1b4e81}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("Multiply color by 1.5")
    {
        std::stringstream ss;
        ss << "div { color: 1.5 * red; background-color: 1.5 * black; border-color: 1.5 * #123456 }";
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

        VERIFY_ERRORS("");

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
"      LIST\n"
"        DECLARATION \"color\"\n"
"          ARG\n"
"            COLOR H:ff0000ff\n"
"        DECLARATION \"background-color\"\n"
"          ARG\n"
"            COLOR H:ff000000\n"
"        DECLARATION \"border-color\"\n"
"          ARG\n"
"            COLOR H:ff814e1b\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{color:red;background-color:#000;border-color:#1b4e81}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("Divide color by 1.5")
    {
        std::stringstream ss;
        ss << "div { color: red / 1.5; background-color: black / 1.5; border-color: #123456 / 1.5 }";
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

        VERIFY_ERRORS("");

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
"      LIST\n"
"        DECLARATION \"color\"\n"
"          ARG\n"
"            COLOR H:aa0000aa\n"
"        DECLARATION \"background-color\"\n"
"          ARG\n"
"            COLOR H:aa000000\n"
"        DECLARATION \"border-color\"\n"
"          ARG\n"
"            COLOR H:aa39230c\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{color:rgba(170,0,0,.67);background-color:rgba(0,0,0,.67);border-color:rgba(12,35,57,.67)}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("Modulo color by 0.7")
    {
        std::stringstream ss;
        ss << "div { color: red % 0.7; background-color: black % 0.7; border-color: #123456 % 0.7 }";
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

        VERIFY_ERRORS("");

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
"      LIST\n"
"        DECLARATION \"color\"\n"
"          ARG\n"
"            COLOR H:4d00004d\n"
"        DECLARATION \"background-color\"\n"
"          ARG\n"
"            COLOR H:4d000000\n"
"        DECLARATION \"border-color\"\n"
"          ARG\n"
"            COLOR H:4d563412\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{color:rgba(77,0,0,.3);background-color:rgba(0,0,0,.3);border-color:rgba(18,52,86,.3)}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("Color * color")
    {
        std::stringstream ss;
        ss << "div { color: red * blue; background-color: frgba(0.3, 0.7, 0.2, 0.5) * frgba(0.9, 0.85, 1.2, 0.5) }";
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

        VERIFY_ERRORS("");

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
"      LIST\n"
"        DECLARATION \"color\"\n"
"          ARG\n"
"            COLOR H:ff000000\n"
"        DECLARATION \"background-color\"\n"
"          ARG\n"
"            COLOR H:403d9845\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{color:#000;background-color:rgba(69,152,61,.25)}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("Color / color")
    {
        std::stringstream ss;
        ss << "div { color: red / #0a0a0a; background-color: frgba(0.3, 0.7, 0.2, 0.5) / frgba(0.9, 0.85, 1.2, 0.5) }";
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

        VERIFY_ERRORS("");

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
"      LIST\n"
"        DECLARATION \"color\"\n"
"          ARG\n"
"            COLOR H:ff0000ff\n"
"        DECLARATION \"background-color\"\n"
"          ARG\n"
"            COLOR H:ff2ad255\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{color:red;background-color:#55d22a}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("Color % color")
    {
        std::stringstream ss;
        ss << "div { color: red % #0a0a0a; background-color: frgba(0.97, 0.85, 1.2, 0.75) % frgba(0.31, 0.7, 0.2, 0.5) }";
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

        VERIFY_ERRORS("");

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
"      LIST\n"
"        DECLARATION \"color\"\n"
"          ARG\n"
"            COLOR H:5\n"
"        DECLARATION \"background-color\"\n"
"          ARG\n"
"            COLOR H:4000260a\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==
"div{color:transparent;background-color:rgba(10,38,0,.25)}\n"
+ csspp_test::get_close_comment()
                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Invalid expressions with colors", "[expression] [multiplicative] [colors] [invalid]")
{
    CATCH_START_SECTION("Divide 5 by a color is not valid")
    {
        std::stringstream ss;
        ss << "div { border-top-left-color: 5 / teal; }";
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

        VERIFY_ERRORS("test.css(1): error: 'number / color' and 'number % color' are not available.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("Modulo 5 by a color is not valid")
    {
        std::stringstream ss;
        ss << "div { border-top-left-color: 5 % teal; }";
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

        VERIFY_ERRORS("test.css(1): error: 'number / color' and 'number % color' are not available.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("Divide 5.8 by a color is not valid")
    {
        std::stringstream ss;
        ss << "div { border-top-left-color: 5.8 / teal; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

//std::cerr << "parse [" << ss.str() << "]\n";
        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

        VERIFY_ERRORS("test.css(1): error: 'number / color' and 'number % color' are not available.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("Modulo 5.8 by a color is not valid")
    {
        std::stringstream ss;
        ss << "div { border-top-left-color: 5.8 % teal; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

//std::cerr << "parse [" << ss.str() << "]\n";
        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

        VERIFY_ERRORS("test.css(1): error: 'number / color' and 'number % color' are not available.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("Multiply 3px by a color is not valid")
    {
        std::stringstream ss;
        ss << "div { border-top-left-color: 3px * chocolate; }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

//std::cerr << "parse [" << ss.str() << "]\n";
        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

        VERIFY_ERRORS("test.css(1): error: color factors must be unit less values, 3px is not acceptable.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("Color division by zero")
    {
        std::stringstream ss;
        ss << "div { border-top-left-color: teal / blue; }";
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

        VERIFY_ERRORS("test.css(1): error: color division does not accept any color component set to zero.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("Color modulo by zero")
    {
        std::stringstream ss;
        ss << "div { border-top-left-color: teal % blue; }";
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

        VERIFY_ERRORS("test.css(1): error: color modulo does not accept any color component set to zero.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    // still no errors?
    VERIFY_ERRORS("");
}

// vim: ts=4 sw=4 et
