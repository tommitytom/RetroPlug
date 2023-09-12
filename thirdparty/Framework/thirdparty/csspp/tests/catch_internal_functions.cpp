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
 * \brief Test the internal_functions.cpp file.
 *
 * This test runs a battery of tests agains internal_functions.cpp
 * to ensure full coverage and that all the internal functions are
 * checked for the equality CSS Preprocessor extensions.
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
#include    <cmath>
#include    <iomanip>
#include    <sstream>


// last include
//
#include    <snapdev/poison.h>



CATCH_TEST_CASE("Expression calc()", "[expression] [internal-functions] [calc]")
{
    CATCH_START_SECTION("calc() -- leave that one alone!")
    {
        std::stringstream ss;
        ss << "div { width: calc(3px + 5%); }";
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

        VERIFY_ERRORS("");

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
"          FUNCTION \"calc\"\n"
"            ARG\n"
"              INTEGER \"px\" I:3\n"
"              WHITESPACE\n"
"              ADD\n"
"              WHITESPACE\n"
"              PERCENT D:0.05\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==

"div{width:calc(3px + 5%)}\n"
+ csspp_test::get_close_comment()

                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Expression cos()/sin()/tan()", "[expression] [internal-functions] [cos] [sin] [tan]")
{
    CATCH_START_SECTION("cos(pi)")
    {
        for(int angle(-180); angle <= 180; angle += rand() % 25 + 1)
        {
            // unspecified (defaults to degrees)
            {
                std::stringstream ss;
                ss << "div { z-index: cos("
                   << angle
                   << "); }";
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
"          DECIMAL_NUMBER \"\" D:" + csspp::decimal_number_to_string(cos(angle * M_PI / 180.0), false) + "\n"
+ csspp_test::get_close_comment(true)

                    );

                std::stringstream assembler_out;
                csspp::assembler a(assembler_out);
                a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

                CATCH_REQUIRE(assembler_out.str() ==

std::string("div{z-index:") + csspp::decimal_number_to_string(cos(angle * M_PI / 180.0), true) + "}\n"
+ csspp_test::get_close_comment()

                        );

                CATCH_REQUIRE(c.get_root() == n);
            }

            // degrees
            {
                std::stringstream ss;
                ss << "div { z-index: cos("
                   << angle
                   << "deg); }";
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
"          DECIMAL_NUMBER \"\" D:" + csspp::decimal_number_to_string(cos(angle * M_PI / 180.0), false) + "\n"
+ csspp_test::get_close_comment(true)

                    );

                std::stringstream assembler_out;
                csspp::assembler a(assembler_out);
                a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

                CATCH_REQUIRE(assembler_out.str() ==

std::string("div{z-index:") + csspp::decimal_number_to_string(cos(angle * M_PI / 180.0), true) + "}\n"
+ csspp_test::get_close_comment()

                        );

                CATCH_REQUIRE(c.get_root() == n);
            }

            // radians
            {
                std::stringstream ss;
                ss << "div { z-index: cos("
                   << angle * M_PI / 180.0
                   << "rad); }";
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
"          DECIMAL_NUMBER \"\" D:" + csspp::decimal_number_to_string(cos(angle * M_PI / 180.0), false) + "\n"
+ csspp_test::get_close_comment(true)

                    );

                std::stringstream assembler_out;
                csspp::assembler a(assembler_out);
                a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

                CATCH_REQUIRE(assembler_out.str() ==

std::string("div{z-index:") + csspp::decimal_number_to_string(cos(angle * M_PI / 180.0), true) + "}\n"
+ csspp_test::get_close_comment()

                        );

                CATCH_REQUIRE(c.get_root() == n);
            }

            // gradians
            {
                std::stringstream ss;
                ss << "div { z-index: cos("
                   << angle * 200 / 180.0
                   << "grad); }";
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
"          DECIMAL_NUMBER \"\" D:" + csspp::decimal_number_to_string(cos(angle * M_PI / 180.0), false) + "\n"
+ csspp_test::get_close_comment(true)

                    );

                std::stringstream assembler_out;
                csspp::assembler a(assembler_out);
                a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

                CATCH_REQUIRE(assembler_out.str() ==

std::string("div{z-index:") + csspp::decimal_number_to_string(cos(angle * M_PI / 180.0), true) + "}\n"
+ csspp_test::get_close_comment()

                        );

                CATCH_REQUIRE(c.get_root() == n);
            }

            // turns
            {
                std::stringstream ss;
                ss << "div { z-index: cos("
                   << angle / 360.0
                   << "turn); }";
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
"          DECIMAL_NUMBER \"\" D:" + csspp::decimal_number_to_string(cos(angle * M_PI / 180.0), false) + "\n"
+ csspp_test::get_close_comment(true)

                    );

                std::stringstream assembler_out;
                csspp::assembler a(assembler_out);
                a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

                CATCH_REQUIRE(assembler_out.str() ==

std::string("div{z-index:") + csspp::decimal_number_to_string(cos(angle * M_PI / 180.0), true) + "}\n"
+ csspp_test::get_close_comment()

                        );

                CATCH_REQUIRE(c.get_root() == n);
            }
        }
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("sin(pi)")
    {
        for(int angle(-180); angle <= 180; angle += rand() % 12)
        {
            // unspecified (defaults to degrees)
            {
                std::stringstream ss;
                ss << "div { z-index: sin("
                   << angle
                   << "); }";
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
"          DECIMAL_NUMBER \"\" D:" + csspp::decimal_number_to_string(sin(angle * M_PI / 180.0), false) + "\n"
+ csspp_test::get_close_comment(true)

                    );

                std::stringstream assembler_out;
                csspp::assembler a(assembler_out);
                a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

                CATCH_REQUIRE(assembler_out.str() ==

std::string("div{z-index:") + csspp::decimal_number_to_string(sin(angle * M_PI / 180.0), true) + "}\n"
+ csspp_test::get_close_comment()

                        );

                CATCH_REQUIRE(c.get_root() == n);
            }

            // degrees
            {
                std::stringstream ss;
                ss << "div { z-index: sin("
                   << angle
                   << "deg); }";
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
"          DECIMAL_NUMBER \"\" D:" + csspp::decimal_number_to_string(sin(angle * M_PI / 180.0), false) + "\n"
+ csspp_test::get_close_comment(true)

                    );

                std::stringstream assembler_out;
                csspp::assembler a(assembler_out);
                a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

                CATCH_REQUIRE(assembler_out.str() ==

std::string("div{z-index:") + csspp::decimal_number_to_string(sin(angle * M_PI / 180.0), true) + "}\n"
+ csspp_test::get_close_comment()

                        );

                CATCH_REQUIRE(c.get_root() == n);
            }

            // radians
            {
                std::stringstream ss;
                ss << "div { z-index: sin("
                   << angle * M_PI / 180.0
                   << "rad); }";
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
"          DECIMAL_NUMBER \"\" D:" + csspp::decimal_number_to_string(sin(angle * M_PI / 180.0), false) + "\n"
+ csspp_test::get_close_comment(true)

                    );

                std::stringstream assembler_out;
                csspp::assembler a(assembler_out);
                a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

                CATCH_REQUIRE(assembler_out.str() ==

std::string("div{z-index:") + csspp::decimal_number_to_string(sin(angle * M_PI / 180.0), true) + "}\n"
+ csspp_test::get_close_comment()

                        );

                CATCH_REQUIRE(c.get_root() == n);
            }

            // gradians
            {
                std::stringstream ss;
                ss << "div { z-index: sin("
                   << angle * 200 / 180.0
                   << "grad); }";
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
"          DECIMAL_NUMBER \"\" D:" + csspp::decimal_number_to_string(sin(angle * M_PI / 180.0), false) + "\n"
+ csspp_test::get_close_comment(true)

                    );

                std::stringstream assembler_out;
                csspp::assembler a(assembler_out);
                a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

                CATCH_REQUIRE(assembler_out.str() ==

std::string("div{z-index:") + csspp::decimal_number_to_string(sin(angle * M_PI / 180.0), true) + "}\n"
+ csspp_test::get_close_comment()

                        );

                CATCH_REQUIRE(c.get_root() == n);
            }

            // turns
            {
                std::stringstream ss;
                ss << "div { z-index: sin("
                   << angle / 360.0
                   << "turn); }";
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
"          DECIMAL_NUMBER \"\" D:" + csspp::decimal_number_to_string(sin(angle * M_PI / 180.0), false) + "\n"
+ csspp_test::get_close_comment(true)

                    );

                std::stringstream assembler_out;
                csspp::assembler a(assembler_out);
                a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

                CATCH_REQUIRE(assembler_out.str() ==

std::string("div{z-index:") + csspp::decimal_number_to_string(sin(angle * M_PI / 180.0), true) + "}\n"
+ csspp_test::get_close_comment()

                        );

                CATCH_REQUIRE(c.get_root() == n);
            }
        }
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("tan(pi)")
    {
        for(int angle(-180); angle <= 180; angle += rand() % 12)
        {

            // unspecified (defaults to degrees)
            {
                std::stringstream ss;
                ss << "div { z-index: tan("
                   << angle
                   << "); }";
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
"          DECIMAL_NUMBER \"\" D:" + csspp::decimal_number_to_string(tan(angle * M_PI / 180.0), false) + "\n"
+ csspp_test::get_close_comment(true)

                    );

                std::stringstream assembler_out;
                csspp::assembler a(assembler_out);
                a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

                CATCH_REQUIRE(assembler_out.str() ==

std::string("div{z-index:") + csspp::decimal_number_to_string(tan(angle * M_PI / 180.0), true) + "}\n"
+ csspp_test::get_close_comment()

                        );

                CATCH_REQUIRE(c.get_root() == n);
            }

            // degrees
            {
                std::stringstream ss;
                ss << "div { z-index: tan("
                   << angle
                   << "deg); }";
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
"          DECIMAL_NUMBER \"\" D:" + csspp::decimal_number_to_string(tan(angle * M_PI / 180.0), false) + "\n"
+ csspp_test::get_close_comment(true)

                    );

                std::stringstream assembler_out;
                csspp::assembler a(assembler_out);
                a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

                CATCH_REQUIRE(assembler_out.str() ==

std::string("div{z-index:") + csspp::decimal_number_to_string(tan(angle * M_PI / 180.0), true) + "}\n"
+ csspp_test::get_close_comment()

                        );

                CATCH_REQUIRE(c.get_root() == n);
            }

            // radians
            {
                std::stringstream rad;
                rad << angle * M_PI / 180.0;
                std::stringstream ss;
                ss << "div { z-index: tan("
                   << rad.str()
                   << "rad); }";
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

                std::string const r(rad.str());
                csspp::decimal_number_t rd(atof(r.c_str()));

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
"          DECIMAL_NUMBER \"\" D:" + csspp::decimal_number_to_string(tan(rd), false) + "\n"
+ csspp_test::get_close_comment(true)

                    );

                std::stringstream assembler_out;
                csspp::assembler a(assembler_out);
                a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

                CATCH_REQUIRE(assembler_out.str() ==

std::string("div{z-index:") + csspp::decimal_number_to_string(tan(rd), true) + "}\n"
+ csspp_test::get_close_comment()

                        );

                CATCH_REQUIRE(c.get_root() == n);
            }

            // gradians
            {
                std::stringstream grad;
                grad << angle * 200.0 / 180.0;
                std::stringstream ss;
                ss << "div { z-index: tan("
                   << grad.str()
                   << "grad); }";
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

                std::string const g(grad.str());
                csspp::decimal_number_t gd(atof(g.c_str()));

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
"          DECIMAL_NUMBER \"\" D:" + csspp::decimal_number_to_string(tan(gd * M_PI / 200.0), false) + "\n"
+ csspp_test::get_close_comment(true)

                    );

                std::stringstream assembler_out;
                csspp::assembler a(assembler_out);
                a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

                CATCH_REQUIRE(assembler_out.str() ==

std::string("div{z-index:") + csspp::decimal_number_to_string(tan(gd * M_PI / 200.0), true) + "}\n"
+ csspp_test::get_close_comment()

                        );

                CATCH_REQUIRE(c.get_root() == n);
            }

            // turns
            {
                std::stringstream turn;
                turn << angle / 360.0;
                std::stringstream ss;
                ss << "div { z-index: tan("
                   << turn.str()
                   << "turn); }";
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

                std::string const t(turn.str());
                csspp::decimal_number_t tn(atof(t.c_str()));

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
"          DECIMAL_NUMBER \"\" D:" + csspp::decimal_number_to_string(tan(tn * M_PI * 2.0), false) + "\n"
+ csspp_test::get_close_comment(true)

                    );

                std::stringstream assembler_out;
                csspp::assembler a(assembler_out);
                a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

                CATCH_REQUIRE(assembler_out.str() ==

std::string("div{z-index:") + csspp::decimal_number_to_string(tan(tn * M_PI * 2.0), true) + "}\n"
+ csspp_test::get_close_comment()

                        );

                CATCH_REQUIRE(c.get_root() == n);
            }

        }
    }
    CATCH_END_SECTION()

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Expression acos()/asin()/atan()", "[expression] [internal-functions] [acos] [asin] [atan]")
{
    CATCH_START_SECTION("acos(ratio)")
    {
        for(int angle(-180); angle <= 180; angle += rand() % 25 + 1)
        {
            std::stringstream ss;
            ss << "div { z-index: acos("
               << cos(angle * M_PI / 180.0)
               << "rad); }";
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
"          DECIMAL_NUMBER \"rad\" D:" + csspp::decimal_number_to_string(labs(angle) * M_PI / 180.0, false) + "\n"
+ csspp_test::get_close_comment(true)

                );

            std::stringstream assembler_out;
            csspp::assembler a(assembler_out);
            a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

            CATCH_REQUIRE(assembler_out.str() ==

std::string("div{z-index:") + csspp::decimal_number_to_string(labs(angle) * M_PI / 180.0, true) + "rad}\n"
+ csspp_test::get_close_comment()

                    );

            CATCH_REQUIRE(c.get_root() == n);
        }

        // another test with an integer
        {
            std::stringstream ss;
            ss << "div { z-index: acos(2); }";
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
"          DECIMAL_NUMBER \"rad\" D:" + csspp::decimal_number_to_string(acos(2.0), false) + "\n"
+ csspp_test::get_close_comment(true)

                );

            std::stringstream assembler_out;
            csspp::assembler a(assembler_out);
            a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

            CATCH_REQUIRE(assembler_out.str() ==

std::string("div{z-index:") + csspp::decimal_number_to_string(acos(2), true) + "rad}\n"
+ csspp_test::get_close_comment()

                    );

            CATCH_REQUIRE(c.get_root() == n);
        }
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("asin(pi)")
    {
        for(int angle(-180); angle <= 180; angle += rand() % 12)
        {
            std::stringstream ss;
            ss << "div { z-index: asin("
               << sin(angle * M_PI / 180.0)
               << "rad); }";
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
"          DECIMAL_NUMBER \"rad\" D:" + csspp::decimal_number_to_string(asin(sin(angle * M_PI / 180.0)), false) + "\n"
+ csspp_test::get_close_comment(true)

                );

            std::stringstream assembler_out;
            csspp::assembler a(assembler_out);
            a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

            CATCH_REQUIRE(assembler_out.str() ==

std::string("div{z-index:") + csspp::decimal_number_to_string(asin(sin(angle * M_PI / 180.0)), true) + "rad}\n"
+ csspp_test::get_close_comment()

                    );

            CATCH_REQUIRE(c.get_root() == n);
        }

        // another test with an integer
        {
            std::stringstream ss;
            ss << "div { z-index: asin(2); }";
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
"          DECIMAL_NUMBER \"rad\" D:" + csspp::decimal_number_to_string(asin(2.0), false) + "\n"
+ csspp_test::get_close_comment(true)

                );

            std::stringstream assembler_out;
            csspp::assembler a(assembler_out);
            a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

            CATCH_REQUIRE(assembler_out.str() ==

std::string("div{z-index:") + csspp::decimal_number_to_string(asin(2), true) + "rad}\n"
+ csspp_test::get_close_comment()

                    );

            CATCH_REQUIRE(c.get_root() == n);
        }
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("atan(pi)")
    {
        for(int angle(-180); angle <= 180; angle += rand() % 12)
        {
            std::stringstream ss;
            ss << "div { z-index: atan("
               << tan(angle * M_PI / 180.0)
               << "); }";
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
"          DECIMAL_NUMBER \"rad\" D:" + csspp::decimal_number_to_string(atan(tan(angle * M_PI / 180.0)), false) + "\n"
+ csspp_test::get_close_comment(true)

                );

            std::stringstream assembler_out;
            csspp::assembler a(assembler_out);
            a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

            CATCH_REQUIRE(assembler_out.str() ==

std::string("div{z-index:") + csspp::decimal_number_to_string(atan(tan(angle * M_PI / 180.0)), true) + "rad}\n"
+ csspp_test::get_close_comment()

                    );

            CATCH_REQUIRE(c.get_root() == n);
        }

        // another test with an integer
        {
            std::stringstream ss;
            ss << "div { z-index: atan(2); }";
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
"          DECIMAL_NUMBER \"rad\" D:" + csspp::decimal_number_to_string(atan(2.0), false) + "\n"
+ csspp_test::get_close_comment(true)

                );

            std::stringstream assembler_out;
            csspp::assembler a(assembler_out);
            a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

            CATCH_REQUIRE(assembler_out.str() ==

std::string("div{z-index:") + csspp::decimal_number_to_string(atan(2), true) + "rad}\n"
+ csspp_test::get_close_comment()

                    );

            CATCH_REQUIRE(c.get_root() == n);
        }
    }
    CATCH_END_SECTION()

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Expression abs()/ceil()/floor()/round()/log()/sign()/sqrt()", "[expression] [internal-functions] [abs] [ceil] [floor] [round]")
{
    CATCH_START_SECTION("abs(number)")
    {
        for(int number(-10000); number <= 10000; number += rand() % 250 + 1)
        {
            // abs(int)
            {
                std::string const dimension(rand() & 1 ? "cm" : "mm");
                std::stringstream ss;
                ss << "div { width: abs("
                   << number
                   << dimension
                   << "); }";
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
"          INTEGER \"" + dimension + "\" I:" + std::to_string(labs(number)) + "\n"
+ csspp_test::get_close_comment(true)

                    );

                std::stringstream assembler_out;
                csspp::assembler a(assembler_out);
                a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

                CATCH_REQUIRE(assembler_out.str() ==

std::string("div{width:") + std::to_string(labs(number)) + dimension + "}\n"
+ csspp_test::get_close_comment()

                        );

                CATCH_REQUIRE(c.get_root() == n);
            }

            // abs(float)
            {
                std::string const dimension(rand() & 1 ? "em" : "px");
                std::stringstream ss;
                ss << "div { width: abs("
                   << std::setprecision(6) << std::fixed
                   << (static_cast<csspp::decimal_number_t>(number) / 1000.0)
                   << dimension
                   << "); }";
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
"          DECIMAL_NUMBER \"" + dimension + "\" D:" + csspp::decimal_number_to_string(fabs(static_cast<csspp::decimal_number_t>(number) / 1000.0), false) + "\n"
+ csspp_test::get_close_comment(true)

                    );

                std::stringstream assembler_out;
                csspp::assembler a(assembler_out);
                a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

                CATCH_REQUIRE(assembler_out.str() ==

std::string("div{width:") + csspp::decimal_number_to_string(fabs(static_cast<csspp::decimal_number_t>(number) / 1000.0), true) + dimension + "}\n"
+ csspp_test::get_close_comment()

                        );

                CATCH_REQUIRE(c.get_root() == n);
            }
        }
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("ceil(number)")
    {
        for(int number(-10000); number <= 10000; number += rand() % 250 + 1)
        {
            // ceil(int)
            {
                std::stringstream ss;
                ss << "div { z-index: ceil("
                   << number
                   << "); }";
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
"          INTEGER \"\" I:" + std::to_string(number) + "\n"
+ csspp_test::get_close_comment(true)

                    );

                std::stringstream assembler_out;
                csspp::assembler a(assembler_out);
                a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

                CATCH_REQUIRE(assembler_out.str() ==

std::string("div{z-index:") + std::to_string(number) + "}\n"
+ csspp_test::get_close_comment()

                        );

                CATCH_REQUIRE(c.get_root() == n);
            }

            // ceil(float)
            {
                std::string const dimension(rand() & 1 ? "deg" : "rad");
                std::stringstream ss;
                ss << "div { z-index: ceil("
                   << std::setprecision(6) << std::fixed
                   << (static_cast<csspp::decimal_number_t>(number) / 1000.0)
                   << dimension
                   << "); }";
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
"          DECIMAL_NUMBER \"" + dimension + "\" D:" + csspp::decimal_number_to_string(ceil(static_cast<csspp::decimal_number_t>(number) / 1000.0), false) + "\n"
+ csspp_test::get_close_comment(true)

                    );

                std::stringstream assembler_out;
                csspp::assembler a(assembler_out);
                a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

                CATCH_REQUIRE(assembler_out.str() ==

std::string("div{z-index:") + csspp::decimal_number_to_string(ceil(static_cast<csspp::decimal_number_t>(number) / 1000.0), true) + dimension + "}\n"
+ csspp_test::get_close_comment()

                        );

                CATCH_REQUIRE(c.get_root() == n);
            }
        }
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("floor(number)")
    {
        for(int number(-10000); number <= 10000; number += rand() % 250 + 1)
        {
            // floor(int)
            {
                std::stringstream ss;
                ss << "div { z-index: floor("
                   << number
                   << "); }";
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
"          INTEGER \"\" I:" + std::to_string(number) + "\n"
+ csspp_test::get_close_comment(true)

                    );

                std::stringstream assembler_out;
                csspp::assembler a(assembler_out);
                a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

                CATCH_REQUIRE(assembler_out.str() ==

std::string("div{z-index:") + std::to_string(number) + "}\n"
+ csspp_test::get_close_comment()

                        );

                CATCH_REQUIRE(c.get_root() == n);
            }

            // floor(float)
            {
                std::string const dimension(rand() & 1 ? "em" : "px");
                std::stringstream ss;
                ss << "div { width: floor("
                   << std::setprecision(6) << std::fixed
                   << (static_cast<csspp::decimal_number_t>(number) / 1000.0)
                   << dimension
                   << "); }";
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
"          DECIMAL_NUMBER \"" + dimension + "\" D:" + csspp::decimal_number_to_string(floor(static_cast<csspp::decimal_number_t>(number) / 1000.0), false) + "\n"
+ csspp_test::get_close_comment(true)

                    );

                std::stringstream assembler_out;
                csspp::assembler a(assembler_out);
                a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

                CATCH_REQUIRE(assembler_out.str() ==

std::string("div{width:") + csspp::decimal_number_to_string(floor(static_cast<csspp::decimal_number_t>(number) / 1000.0), true) + dimension + "}\n"
+ csspp_test::get_close_comment()

                        );

                CATCH_REQUIRE(c.get_root() == n);
            }
        }
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("round(number)")
    {
        for(int number(-10000); number <= 10000; number += rand() % 250 + 1)
        {
            // round(int)
            {
                std::stringstream ss;
                ss << "div { z-index: round("
                   << number
                   << "); }";
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
"          INTEGER \"\" I:" + std::to_string(number) + "\n"
+ csspp_test::get_close_comment(true)

                    );

                std::stringstream assembler_out;
                csspp::assembler a(assembler_out);
                a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

                CATCH_REQUIRE(assembler_out.str() ==

std::string("div{z-index:") + std::to_string(number) + "}\n"
+ csspp_test::get_close_comment()

                        );

                CATCH_REQUIRE(c.get_root() == n);
            }

            // round(float)
            {
                std::string const dimension(rand() & 1 ? "px" : "em");
                std::stringstream ss;
                ss << "div { width: round("
                   << std::setprecision(6) << std::fixed
                   << (static_cast<csspp::decimal_number_t>(number) / 1000.0)
                   << dimension
                   << "); }";
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
"          DECIMAL_NUMBER \"" + dimension + "\" D:" + csspp::decimal_number_to_string(round(static_cast<csspp::decimal_number_t>(number) / 1000.0), false) + "\n"
+ csspp_test::get_close_comment(true)

                    );

                std::stringstream assembler_out;
                csspp::assembler a(assembler_out);
                a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

                CATCH_REQUIRE(assembler_out.str() ==

std::string("div{width:") + csspp::decimal_number_to_string(round(static_cast<csspp::decimal_number_t>(number) / 1000.0), true) + dimension + "}\n"
+ csspp_test::get_close_comment()

                        );

                CATCH_REQUIRE(c.get_root() == n);
            }
        }
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("log(number)")
    {
        // log(-1) and log(0) are invalid
        for(int number(1); number <= 10000; number += rand() % 250 + 1)
        {
            // log(int)
            {
                std::stringstream ss;
                ss << "div { z-index: log("
                   << number
                   << "); }";
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
"          DECIMAL_NUMBER \"\" D:" + csspp::decimal_number_to_string(log(static_cast<csspp::decimal_number_t>(number)), false) + "\n"
+ csspp_test::get_close_comment(true)

                    );

                std::stringstream assembler_out;
                csspp::assembler a(assembler_out);
                a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

                CATCH_REQUIRE(assembler_out.str() ==

std::string("div{z-index:") + csspp::decimal_number_to_string(log(static_cast<csspp::decimal_number_t>(number)), false) + "}\n"
+ csspp_test::get_close_comment()

                        );

                CATCH_REQUIRE(c.get_root() == n);
            }

            // log(float)
            {
                std::stringstream ss;
                ss << "div { z-index: log("
                   << std::setprecision(6) << std::fixed
                   << (static_cast<csspp::decimal_number_t>(number) / 1000.0)
                   << "); }";
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
"          DECIMAL_NUMBER \"\" D:" + csspp::decimal_number_to_string(log(static_cast<csspp::decimal_number_t>(number) / 1000.0), false) + "\n"
+ csspp_test::get_close_comment(true)

                    );

                std::stringstream assembler_out;
                csspp::assembler a(assembler_out);
                a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

                CATCH_REQUIRE(assembler_out.str() ==

std::string("div{z-index:") + csspp::decimal_number_to_string(log(static_cast<csspp::decimal_number_t>(number) / 1000.0), true) + "}\n"
+ csspp_test::get_close_comment()

                        );

                CATCH_REQUIRE(c.get_root() == n);
            }
        }
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("sign(number)")
    {
        for(int number(-10000); number <= 10000; number += rand() % 250 + 1)
        {
            // sign(int)
            {
                std::stringstream ss;
                ss << "div { z-index: sign("
                   << number
                   << "); }";
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
"          INTEGER \"\" I:" + std::to_string(number == 0 ? 0 : (number < 0 ? -1 : 1)) + "\n"
+ csspp_test::get_close_comment(true)

                    );

                std::stringstream assembler_out;
                csspp::assembler a(assembler_out);
                a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

                CATCH_REQUIRE(assembler_out.str() ==

"div{z-index:" + std::to_string(number == 0 ? 0 : (number < 0 ? -1 : 1)) + "}\n"
+ csspp_test::get_close_comment()

                        );

                CATCH_REQUIRE(c.get_root() == n);
            }

            // sign(float)
            {
                std::stringstream ss;
                ss << "div { z-index: sign("
                   << std::setprecision(6) << std::fixed
                   << (static_cast<csspp::decimal_number_t>(number) / 1000.0)
                   << "); }";
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
"          DECIMAL_NUMBER \"\" D:" + (number == 0 ? "0" : (number < 0 ? "-1" : "1")) + "\n"
+ csspp_test::get_close_comment(true)

                    );

                std::stringstream assembler_out;
                csspp::assembler a(assembler_out);
                a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

                CATCH_REQUIRE(assembler_out.str() ==

std::string("div{z-index:") + (number == 0 ? "0" : (number < 0 ? "-1" : "1")) + "}\n"
+ csspp_test::get_close_comment()

                        );

                CATCH_REQUIRE(c.get_root() == n);
            }

            // sign(percent)
            {
                std::stringstream ss;
                ss << "div { width: sign("
                   << std::setprecision(6) << std::fixed
                   << (static_cast<csspp::decimal_number_t>(number) / 1000.0)
                   << "%); }";
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
"          PERCENT D:" + (number == 0 ? "0" : (number < 0 ? "-1" : "1")) + "\n"
+ csspp_test::get_close_comment(true)

                    );

                std::stringstream assembler_out;
                csspp::assembler a(assembler_out);
                a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

                CATCH_REQUIRE(assembler_out.str() ==

std::string("div{width:") + (number == 0 ? "0" : (number < 0 ? "-100": "100")) + "%}\n"
+ csspp_test::get_close_comment()

                        );

                CATCH_REQUIRE(c.get_root() == n);
            }
        }
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("sqrt(number)")
    {
        // sqrt(-number) is not valid, so skip negative numbers
        for(int number(0); number <= 10000; number += rand() % 250 + 1)
        {
            // sqrt(int)
            {
                std::stringstream ss;
                ss << "div { z-index: sqrt("
                   << number
                   << "); }";
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
"          DECIMAL_NUMBER \"\" D:" + csspp::decimal_number_to_string(sqrt(static_cast<csspp::decimal_number_t>(number)), false) + "\n"
+ csspp_test::get_close_comment(true)

                    );

                std::stringstream assembler_out;
                csspp::assembler a(assembler_out);
                a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

                CATCH_REQUIRE(assembler_out.str() ==

"div{z-index:" + csspp::decimal_number_to_string(sqrt(static_cast<csspp::decimal_number_t>(number)), true) + "}\n"
+ csspp_test::get_close_comment()

                        );

                CATCH_REQUIRE(c.get_root() == n);
            }

            // sqrt(float)
            {
                std::stringstream ss;
                ss << "div { z-index: sqrt("
                   << std::setprecision(6) << std::fixed
                   << (static_cast<csspp::decimal_number_t>(number) / 1000.0)
                   << "); }";
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
"          DECIMAL_NUMBER \"\" D:" + csspp::decimal_number_to_string(sqrt(static_cast<csspp::decimal_number_t>(number) / 1000.0), false) + "\n"
+ csspp_test::get_close_comment(true)

                    );

                std::stringstream assembler_out;
                csspp::assembler a(assembler_out);
                a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

                CATCH_REQUIRE(assembler_out.str() ==

std::string("div{z-index:") + csspp::decimal_number_to_string(sqrt(static_cast<csspp::decimal_number_t>(number) / 1000.0), true) + "}\n"
+ csspp_test::get_close_comment()

                        );

                CATCH_REQUIRE(c.get_root() == n);
            }

            // sqrt(dimension)
            {
                std::stringstream ss;
                ss << "div { width: sqrt("
                   << std::setprecision(6) << std::fixed
                   << (static_cast<csspp::decimal_number_t>(number) / 1000.0)
                   << "px\\*px); }";
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
"          DECIMAL_NUMBER \"px\" D:" + csspp::decimal_number_to_string(sqrt(static_cast<csspp::decimal_number_t>(number) / 1000.0), false) + "\n"
+ csspp_test::get_close_comment(true)

                    );

                std::stringstream assembler_out;
                csspp::assembler a(assembler_out);
                a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

                CATCH_REQUIRE(assembler_out.str() ==

std::string("div{width:") + csspp::decimal_number_to_string(sqrt(static_cast<csspp::decimal_number_t>(number) / 1000.0), true) + "px}\n"
+ csspp_test::get_close_comment()

                        );

                CATCH_REQUIRE(c.get_root() == n);
            }

            // sqrt(dimension) -- dividend and divisor
            {
                std::stringstream ss;
                ss << "div { width: sqrt("
                   << std::setprecision(6) << std::fixed
                   << (static_cast<csspp::decimal_number_t>(number) / 1000.0)
                   << "px\\*px\\/em\\*em) * 1em; }";
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
"          DECIMAL_NUMBER \"px\" D:" + csspp::decimal_number_to_string(sqrt(static_cast<csspp::decimal_number_t>(number) / 1000.0), false) + "\n"
+ csspp_test::get_close_comment(true)

                    );

                std::stringstream assembler_out;
                csspp::assembler a(assembler_out);
                a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

                CATCH_REQUIRE(assembler_out.str() ==

std::string("div{width:") + csspp::decimal_number_to_string(sqrt(static_cast<csspp::decimal_number_t>(number) / 1000.0), true) + "px}\n"
+ csspp_test::get_close_comment()

                        );

                CATCH_REQUIRE(c.get_root() == n);
            }
        }
    }
    CATCH_END_SECTION()

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Expression red()/green()/blue()/alpha()", "[expression] [internal-functions] [red] [green] [blue] [alpha]")
{
    CATCH_START_SECTION("check color components")
    {
        for(int r(0); r < 256; r += rand() % 100 + 1)
        {
            for(int g(0); g < 256; g += rand() % 100 + 1)
            {
                for(int b(0); b < 256; b += rand() % 100 + 1)
                {
                    for(int alpha(0); alpha < 256; alpha += rand() % 100 + 1)
                    {
                        {
                            std::stringstream ss;
                            ss << "div { z-index: red(rgba("
                               << r
                               << ", "
                               << g
                               << ", "
                               << b
                               << ", "
                               << alpha / 255.0
                               << ")); }\n"
                               << "span { z-index: green(rgba("
                               << r
                               << ", "
                               << g
                               << ", "
                               << b
                               << ", "
                               << alpha / 255.0
                               << ")); }\n"
                               << "p { z-index: blue(rgba("
                               << r
                               << ", "
                               << g
                               << ", "
                               << b
                               << ", "
                               << alpha / 255.0
                               << ")); }\n"
                               << "i { z-index: alpha(rgba("
                               << r
                               << ", "
                               << g
                               << ", "
                               << b
                               << ", "
                               << alpha / 255.0
                               << ")); }\n"
                               << "div { z-index: red(rgb("
                               << r
                               << ", "
                               << g
                               << ", "
                               << b
                               << ")); }\n"
                               << "span { z-index: green(rgb("
                               << r
                               << ", "
                               << g
                               << ", "
                               << b
                               << ")); }\n"
                               << "p { z-index: blue(rgb("
                               << r
                               << ", "
                               << g
                               << ", "
                               << b
                               << ")); }\n"
                               << "i { z-index: alpha(rgb("
                               << r
                               << ", "
                               << g
                               << ", "
                               << b
                               << ")); }\n"
                               << "div { z-index: red(rgba(rgb("
                               << r
                               << ", "
                               << g
                               << ", "
                               << b
                               << "), "
                               << alpha / 255.0
                               << ")); }\n"
                               << "span { z-index: green(rgba(rgb("
                               << r
                               << ", "
                               << g
                               << ", "
                               << b
                               << "), "
                               << alpha / 255.0
                               << ")); }\n"
                               << "p { z-index: blue(rgba(rgb("
                               << r
                               << ", "
                               << g
                               << ", "
                               << b
                               << "), "
                               << alpha / 255.0
                               << ")); }\n"
                               << "i { z-index: alpha(rgba(rgb("
                               << r
                               << ", "
                               << g
                               << ", "
                               << b
                               << "), "
                               << alpha / 255.0
                               << ")); }\n"
                               << "div { z-index: red(frgba("
                               << r / 255.0
                               << ", "
                               << g / 255.0
                               << ", "
                               << b / 255.0
                               << ", "
                               << alpha / 255.0
                               << ")); }\n"
                               << "span { z-index: green(frgba("
                               << r / 255.0
                               << ", "
                               << g / 255.0
                               << ", "
                               << b / 255.0
                               << ", "
                               << alpha / 255.0
                               << ")); }\n"
                               << "p { z-index: blue(frgba("
                               << r / 255.0
                               << ", "
                               << g / 255.0
                               << ", "
                               << b / 255.0
                               << ", "
                               << alpha / 255.0
                               << ")); }\n"
                               << "i { z-index: alpha(frgba("
                               << r / 255.0
                               << ", "
                               << g / 255.0
                               << ", "
                               << b / 255.0
                               << ", "
                               << alpha / 255.0
                               << ")); }\n"
                               << "div { z-index: red(frgb("
                               << r / 255.0
                               << ", "
                               << g / 255.0
                               << ", "
                               << b / 255.0
                               << ")); }\n"
                               << "span { z-index: green(frgb("
                               << r / 255.0
                               << ", "
                               << g / 255.0
                               << ", "
                               << b / 255.0
                               << ")); }\n"
                               << "p { z-index: blue(frgb("
                               << r / 255.0
                               << ", "
                               << g / 255.0
                               << ", "
                               << b / 255.0
                               << ")); }\n"
                               << "i { z-index: alpha(frgb("
                               << r / 255.0
                               << ", "
                               << g / 255.0
                               << ", "
                               << b / 255.0
                               << ")); }\n"
                               << "div { z-index: red(frgba(frgb("
                               << r / 255.0
                               << ", "
                               << g / 255.0
                               << ", "
                               << b / 255.0
                               << "), "
                               << alpha / 255.0
                               << ")); }\n"
                               << "span { z-index: green(frgba(frgb("
                               << r / 255.0
                               << ", "
                               << g / 255.0
                               << ", "
                               << b / 255.0
                               << "), "
                               << alpha / 255.0
                               << ")); }\n"
                               << "p { z-index: blue(frgba(frgb("
                               << r / 255.0
                               << ", "
                               << g / 255.0
                               << ", "
                               << b / 255.0
                               << "), "
                               << alpha / 255.0
                               << ")); }\n"
                               << "i { z-index: alpha(frgba(frgb("
                               << r / 255.0
                               << ", "
                               << g / 255.0
                               << ", "
                               << b / 255.0
                               << "), "
                               << alpha / 255.0
                               << ")); }\n";
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
// component(rgba())
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:" + std::to_string(r) + "\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"span\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:" + std::to_string(g) + "\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"p\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:" + std::to_string(b) + "\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"i\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"\" D:" + csspp::decimal_number_to_string(alpha / 255.0, false) + "\n"
// component(rgb())
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:" + std::to_string(r) + "\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"span\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:" + std::to_string(g) + "\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"p\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:" + std::to_string(b) + "\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"i\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"\" D:1\n"
// component(rgba(rgb(), alpha))
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:" + std::to_string(r) + "\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"span\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:" + std::to_string(g) + "\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"p\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:" + std::to_string(b) + "\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"i\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"\" D:" + csspp::decimal_number_to_string(alpha / 255.0, false) + "\n"
// component(frgba())
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:" + std::to_string(r) + "\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"span\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:" + std::to_string(g) + "\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"p\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:" + std::to_string(b) + "\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"i\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"\" D:" + csspp::decimal_number_to_string(alpha / 255.0, false) + "\n"
// component(frgb())
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:" + std::to_string(r) + "\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"span\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:" + std::to_string(g) + "\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"p\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:" + std::to_string(b) + "\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"i\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"\" D:1\n"
// component(frgba(frgb(), alpha))
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:" + std::to_string(r) + "\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"span\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:" + std::to_string(g) + "\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"p\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:" + std::to_string(b) + "\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"i\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"\" D:" + csspp::decimal_number_to_string(alpha / 255.0, false) + "\n"
+ csspp_test::get_close_comment(true)

                                );

                            std::stringstream assembler_out;
                            csspp::assembler a(assembler_out);
                            a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

                            CATCH_REQUIRE(assembler_out.str() ==

// rgba()
std::string("div{z-index:") + std::to_string(r) + "}"
"span{z-index:" + std::to_string(g) + "}"
"p{z-index:" + std::to_string(b) + "}"
"i{z-index:" + csspp::decimal_number_to_string(alpha / 255.0, true) + "}"
// rbg()
"div{z-index:" + std::to_string(r) + "}"
"span{z-index:" + std::to_string(g) + "}"
"p{z-index:" + std::to_string(b) + "}"
"i{z-index:1}"
// rgba(rgb(), alpha)
"div{z-index:" + std::to_string(r) + "}"
"span{z-index:" + std::to_string(g) + "}"
"p{z-index:" + std::to_string(b) + "}"
"i{z-index:" + csspp::decimal_number_to_string(alpha / 255.0, true) + "}"
// frgba()
"div{z-index:" + std::to_string(r) + "}"
"span{z-index:" + std::to_string(g) + "}"
"p{z-index:" + std::to_string(b) + "}"
"i{z-index:" + csspp::decimal_number_to_string(alpha / 255.0, true) + "}"
// frgb()
"div{z-index:" + std::to_string(r) + "}"
"span{z-index:" + std::to_string(g) + "}"
"p{z-index:" + std::to_string(b) + "}"
"i{z-index:1}"
// frgba(frgb(), alpha)
"div{z-index:" + std::to_string(r) + "}"
"span{z-index:" + std::to_string(g) + "}"
"p{z-index:" + std::to_string(b) + "}"
"i{z-index:" + csspp::decimal_number_to_string(alpha / 255.0, true) + "}"
"\n"
+ csspp_test::get_close_comment()

                                    );

                            CATCH_REQUIRE(c.get_root() == n);
                        }
                    }
                }
            }
        }
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("rgb/rgba/frgb/frgba from #color")
    {
        std::stringstream ss;
        ss << "div  { z-index: red(  rgba( darkolivegreen, 0.5)); }\n"
           << "span { z-index: green(rgba( darkolivegreen, 0.5)); }\n"
           << "p    { z-index: blue( rgba( darkolivegreen, 0.5)); }\n"
           << "i    { z-index: alpha(rgba( darkolivegreen, 0.5)); }\n"
           << "div  { z-index: red(  rgb(  deeppink)); }\n"
           << "span { z-index: green(rgb(  deeppink)); }\n"
           << "p    { z-index: blue( rgb(  deeppink)); }\n"
           << "i    { z-index: alpha(rgb(  deeppink)); }\n"
           << "div  { z-index: red(  frgba(ghostwhite, 0.5)); }\n"
           << "span { z-index: green(frgba(ghostwhite, 0.5)); }\n"
           << "p    { z-index: blue( frgba(ghostwhite, 0.5)); }\n"
           << "i    { z-index: alpha(frgba(ghostwhite, 0.5)); }\n"
           << "div  { z-index: red(  frgb( hotpink)); }\n"
           << "span { z-index: green(frgb( hotpink)); }\n"
           << "p    { z-index: blue( frgb( hotpink)); }\n"
           << "i    { z-index: alpha(frgb( hotpink)); }\n";
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
// component(rgba(darkolivegreen, 0.5))
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:85\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"span\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:107\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"p\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:47\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"i\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"\" D:0.5\n"
// component(rgb(deeppink))
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:255\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"span\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:20\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"p\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:147\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"i\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"\" D:1\n"
// component(frgba(ghostwhite, 0.5))
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:248\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"span\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:248\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"p\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:255\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"i\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"\" D:0.5\n"
// component(frgb(hotpink))
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:255\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"span\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:105\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"p\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:180\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"i\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"\" D:1\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==

// rgba(darkolivegreen, 0.5)
"div{z-index:85}"
"span{z-index:107}"
"p{z-index:47}"
"i{z-index:.5}"
// rbg(deeppink)
"div{z-index:255}"
"span{z-index:20}"
"p{z-index:147}"
"i{z-index:1}"
// rgba(ghostwhite, 0.5)
"div{z-index:248}"
"span{z-index:248}"
"p{z-index:255}"
"i{z-index:.5}"
// frgb(hotpink)
"div{z-index:255}"
"span{z-index:105}"
"p{z-index:180}"
"i{z-index:1}"
"\n"
+ csspp_test::get_close_comment()

                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Expression hue()/saturation()/lightness()/alpha()", "[expression] [internal-functions] [hue] [saturation] [lightness] [alpha]")
{
    CATCH_START_SECTION("check color components")
    {
        for(int h(46); h < 180; h += rand() % 50 + 1)
        {
            for(int s(rand() % 25 + 1); s < 100; s += rand() % 25 + 1)
            {
                for(int l(rand() % 25 + 1); l < 100; l += rand() % 25 + 1)
                {
                    for(int alpha(rand() % 25); alpha < 256; alpha += rand() % 100 + 1)
                    {
                        std::stringstream ss;
                        ss << "div { z-index: hue(hsla("
                           << h
                           << "deg, "
                           << s
                           << "%, "
                           << l
                           << "%, "
                           << alpha / 255.0
                           << ")); }\n"
                           << "span { z-index: saturation(hsla("
                           << h
                           << ", "
                           << s
                           << "%, "
                           << l
                           << "%, "
                           << alpha / 255.0
                           << ")); }\n"
                           << "p { z-index: lightness(hsla("
                           << h
                           << ", "
                           << s
                           << "%, "
                           << l
                           << "%, "
                           << alpha / 255.0
                           << ")); }\n"
                           << "i { z-index: alpha(hsla("
                           << h
                           << ", "
                           << s
                           << "%, "
                           << l
                           << "%, "
                           << alpha / 255.0
                           << ")); }\n"
                           << "div { z-index: hue(hsl("
                           << h
                           << "deg, "
                           << s
                           << "%, "
                           << l
                           << "%)); }\n"
                           << "span { z-index: saturation(hsl("
                           << h
                           << ", "
                           << s
                           << "%, "
                           << l
                           << "%)); }\n"
                           << "p { z-index: lightness(hsl("
                           << h
                           << "deg, "
                           << s
                           << "%, "
                           << l
                           << "%)); }\n"
                           << "i { z-index: alpha(hsl("
                           << h
                           << ", "
                           << s
                           << "%, "
                           << l
                           << "%)); }\n";
                        csspp::position pos("test.css");
                        csspp::lexer::pointer_t lex(new csspp::lexer(ss, pos));

                        csspp::parser p(lex);

                        csspp::node::pointer_t n(p.stylesheet());

                        csspp::compiler c;
                        c.set_root(n);
                        c.set_date_time_variables(csspp_test::get_now());
                        c.add_path(csspp_test::get_script_path());
                        c.add_path(csspp_test::get_version_script_path());

                        c.compile(false);

//std::cerr << "Compiler result is: [" << *c.get_root() << "]\n";

                        // the h,s,l may come back out slight different
                        // so we get the "fixed" value to check in our
                        // returned value
                        csspp::color col;
                        col.set_hsl(fmod(h, 360.0) * M_PI / 180.0, s / 100.0, l / 100.0, alpha);
                        csspp::color_component_t h1, s1, l1, alpha1;
                        col.get_hsl(h1, s1, l1, alpha1);

                        // to verify that the result is still an INTEGER we have to
                        // test the root node here
                        std::stringstream compiler_out;
                        compiler_out << *n;
                        VERIFY_TREES(compiler_out.str(),

"LIST\n"
+ csspp_test::get_default_variables() +
// component(hsla())
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"deg\" D:" + csspp::decimal_number_to_string(h1 * 180.0 / M_PI, false) + "\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"span\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          PERCENT D:" + csspp::decimal_number_to_string(s1, false) + "\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"p\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          PERCENT D:" + csspp::decimal_number_to_string(l1, false) + "\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"i\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"\" D:" + csspp::decimal_number_to_string(alpha / 255.0, false) + "\n"
// component(hsl())
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"deg\" D:" + csspp::decimal_number_to_string(h1 * 180.0 / M_PI, false) + "\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"span\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          PERCENT D:" + csspp::decimal_number_to_string(s1, false) + "\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"p\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          PERCENT D:" + csspp::decimal_number_to_string(l1, false) + "\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"i\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"\" D:1\n"
+ csspp_test::get_close_comment(true)

                            );

                        std::stringstream assembler_out;
                        csspp::assembler a(assembler_out);
                        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

                        CATCH_REQUIRE(assembler_out.str() ==

// hsla()
std::string("div{z-index:") + csspp::decimal_number_to_string(h1 * 180.0 / M_PI, true) + "deg}"
"span{z-index:" + std::to_string(s) + "%}"
"p{z-index:" + std::to_string(l) + "%}"
"i{z-index:" + csspp::decimal_number_to_string(alpha / 255.0, true) + "}"
// hsl()
"div{z-index:" + csspp::decimal_number_to_string(h1 * 180.0 / M_PI, true) + "deg}"
"span{z-index:" + std::to_string(s) + "%}"
"p{z-index:" + std::to_string(l) + "%}"
"i{z-index:1}"
"\n"
+ csspp_test::get_close_comment()

                                );

                        CATCH_REQUIRE(c.get_root() == n);
                    }
                }
            }
        }
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("rgb/rgba/frgb/frgba from #color")
    {
        std::stringstream ss;
        ss << "div  { z-index: red(  rgba( darkolivegreen, 0.5)); }\n"
           << "span { z-index: green(rgba( darkolivegreen, 0.5)); }\n"
           << "p    { z-index: blue( rgba( darkolivegreen, 0.5)); }\n"
           << "i    { z-index: alpha(rgba( darkolivegreen, 0.5)); }\n"
           << "div  { z-index: red(  rgb(  deeppink)); }\n"
           << "span { z-index: green(rgb(  deeppink)); }\n"
           << "p    { z-index: blue( rgb(  deeppink)); }\n"
           << "i    { z-index: alpha(rgb(  deeppink)); }\n"
           << "div  { z-index: red(  frgba(ghostwhite, 0.5)); }\n"
           << "span { z-index: green(frgba(ghostwhite, 0.5)); }\n"
           << "p    { z-index: blue( frgba(ghostwhite, 0.5)); }\n"
           << "i    { z-index: alpha(frgba(ghostwhite, 0.5)); }\n"
           << "div  { z-index: red(  frgb( hotpink)); }\n"
           << "span { z-index: green(frgb( hotpink)); }\n"
           << "p    { z-index: blue( frgb( hotpink)); }\n"
           << "i    { z-index: alpha(frgb( hotpink)); }\n";
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
// component(rgba(darkolivegreen, 0.5))
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:85\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"span\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:107\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"p\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:47\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"i\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"\" D:0.5\n"
// component(rgb(deeppink))
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:255\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"span\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:20\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"p\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:147\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"i\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"\" D:1\n"
// component(frgba(ghostwhite, 0.5))
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:248\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"span\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:248\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"p\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:255\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"i\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"\" D:0.5\n"
// component(frgb(hotpink))
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:255\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"span\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:105\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"p\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:180\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"i\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"\" D:1\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==

// rgba(darkolivegreen, 0.5)
"div{z-index:85}"
"span{z-index:107}"
"p{z-index:47}"
"i{z-index:.5}"
// rbg(deeppink)
"div{z-index:255}"
"span{z-index:20}"
"p{z-index:147}"
"i{z-index:1}"
// rgba(ghostwhite, 0.5)
"div{z-index:248}"
"span{z-index:248}"
"p{z-index:255}"
"i{z-index:.5}"
// frgb(hotpink)
"div{z-index:255}"
"span{z-index:105}"
"p{z-index:180}"
"i{z-index:1}"
"\n"
+ csspp_test::get_close_comment()

                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Expression function_exists()/variable_exists()/global_variable_exists()", "[expression] [internal-functions] [function-exists] [variable-exists] [global-variable-exists]")
{
    CATCH_START_SECTION("check existance of internal functions")
    {
        // list of internal functions, they all must return true
        // those that start with '*' are colors that are viewed
        // as functions when followed by '(' but color otherwise
        char const * internal_functions[] =
        {
            "abs",
            "acos",
            "alpha",
            "asin",
            "atan",
            "*blue",
            "ceil",
            "cos",
            "decimal_number",
            "floor",
            "frgb",
            "frgba",
            "function_exists",
            "global_variable_exists",
            "*green",
            "hsl",
            "hsla",
            "hue",
            "identifier",
            "if",
            "integer",
            "inspect",
            "lightness",
            "log",
            "max",
            "min",
            "not",
            "percentage",
            "random",
            "*red",
            "rgb",
            "rgba",
            "round",
            "saturation",
            "sign",
            "sin",
            "sqrt",
            "string",
            "str_length",
            "*tan",
            "type_of",
            "unique_id",
            "unit",
            "variable_exists"
        };

        for(size_t idx(0); idx < sizeof(internal_functions) / sizeof(internal_functions[0]); ++idx)
        {
            bool use_string(false);
            char const *name = internal_functions[idx];
            if(*name == '*')
            {
                use_string = true;
                ++name;
            }
            std::stringstream ss;
            ss << "div { z-index: function_exists("
               << (use_string ? "\"" : "")
               << name
               << (use_string ? "\"" : "")
               << ") ? decimal_number(\"3.14\") : 17 }\n"
               << "div { z-index: function_exists(\""
               << name
               << "\") ? decimal_number(\"3.14\") : 17 }\n"
               << "div { z-index: function_exists(\"but_not_"
               << name
               << "\") ? decimal_number(\"3.14\") : 17 }\n";
//std::cerr << "*** input = [" << ss.str() << "]\n";
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
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"\" D:3.14\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"\" D:3.14\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:17\n"
+ csspp_test::get_close_comment(true)

                );

            std::stringstream assembler_out;
            csspp::assembler a(assembler_out);
            a.output(n, csspp::output_mode_t::COMPRESSED);

    //std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

            CATCH_REQUIRE(assembler_out.str() ==

"div{z-index:3.14}div{z-index:3.14}div{z-index:17}\n"
+ csspp_test::get_close_comment()

                    );

            CATCH_REQUIRE(c.get_root() == n);
        }
    }
    CATCH_END_SECTION()

    // system defined functions are just like user defined functions
    // so we don't have to test more (although these are only global
    // functions, we could add a test to verify that functions defined
    // in a {}-block are ignored from outside that block.)
    CATCH_START_SECTION("check existance of system functions")
    {
        // list of system functions, they all must return true
        // those that start with '*' are colors that are viewed
        // as functions when followed by '(' but color otherwise
        char const * internal_functions[] =
        {
            "adjust_hue",
            "complement",
            "darken",
            "deg2rad",
            "desaturate",
            "fade_in",
            "fade_out",
            "grayscale",
            "invert",
            "lighten",
            "mix",
            "opacify",
            "opacity",
            "quote",
            "remove_unit",
            "saturate",
            "set_unit",
            "transparentize",
            "unitless",
            "unquote"
        };

        for(size_t idx(0); idx < sizeof(internal_functions) / sizeof(internal_functions[0]); ++idx)
        {
            bool use_string(false);
            char const *name = internal_functions[idx];
            if(*name == '*')
            {
                use_string = true;
                ++name;
            }
            std::stringstream ss;
            ss << "div { z-index: function_exists("
               << (use_string ? "\"" : "")
               << name
               << (use_string ? "\"" : "")
               << ") ? decimal_number(\"3.14\") : 17 }\n"
               << "div { z-index: function_exists(\""
               << name
               << "\") ? decimal_number(\"3.14\") : 17 }\n"
               << "div { z-index: function_exists(\"but_"
               << name
               << "_does_not_exist\") ? decimal_number(\"3.14\") : 17 }\n";
//std::cerr << "*** input = [" << ss.str() << "]\n";
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
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"\" D:3.14\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"\" D:3.14\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:17\n"
+ csspp_test::get_close_comment(true)

                );

            std::stringstream assembler_out;
            csspp::assembler a(assembler_out);
            a.output(n, csspp::output_mode_t::COMPRESSED);

    //std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

            CATCH_REQUIRE(assembler_out.str() ==

"div{z-index:3.14}div{z-index:3.14}div{z-index:17}\n"
+ csspp_test::get_close_comment()

                    );

            CATCH_REQUIRE(c.get_root() == n);
        }
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("check that the system defined variables exist")
    {
        // list of system variables
        char const * internal_variables[] =
        {
            // version.scss
            "_csspp_version",
            "_csspp_major",
            "_csspp_minor",
            "_csspp_patch",

            // constants.scss
            "_csspp_e",
            "_csspp_log2e",
            "_csspp_log10e",
            "_csspp_ln2e",
            "_csspp_ln10e",
            "_csspp_pi",
            "_csspp_pi_rad",
            "_csspp_sqrt2",

            // internal (generated by library)
            "_csspp_no_logo",
            "_csspp_usdate",
            "_csspp_month",
            "_csspp_day",
            "_csspp_year",
            "_csspp_time",
            "_csspp_hour",
            "_csspp_minute",
            "_csspp_second",
        };

        for(size_t idx(0); idx < sizeof(internal_variables) / sizeof(internal_variables[0]); ++idx)
        {
            char const *name = internal_variables[idx];
            std::stringstream ss;
            ss << "div { z-index: global_variable_exists("
               << name
               << ") ? decimal_number(\"3.14\") : 17 }\n"
               << "div { z-index: global_variable_exists(\""
               << name
               << "\") ? decimal_number(\"3.14\") : 17 }\n"
               << "div { z-index: global_variable_exists(\""
               << name
               << "_not_this_one\") ? decimal_number(\"3.14\") : 17 }\n"
               // finally, test with a sub-variable which exists
               << "div { $sub_var: 123; z-index: global_variable_exists(\"sub_var\") ? decimal_number(\"3.14\") : 17 }\n";
//std::cerr << "*** input = [" << ss.str() << "]\n";
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
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"\" D:3.14\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"\" D:3.14\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:17\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"        V:sub_var\n"
"          LIST\n"
"            VARIABLE \"sub_var\"\n"
"            INTEGER \"\" I:123\n"
"      LIST\n"
"        DECLARATION \"z-index\"\n"
"          ARG\n"
"            INTEGER \"\" I:17\n"
+ csspp_test::get_close_comment(true)

                );

            std::stringstream assembler_out;
            csspp::assembler a(assembler_out);
            a.output(n, csspp::output_mode_t::COMPRESSED);

    //std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

            CATCH_REQUIRE(assembler_out.str() ==

"div{z-index:3.14}div{z-index:3.14}div{z-index:17}div{z-index:17}\n"
+ csspp_test::get_close_comment()

                    );

            CATCH_REQUIRE(c.get_root() == n);
        }
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("check that various variables exist")
    {
        // list of system variables
        char const * internal_variables[] =
        {
            // version.scss
            "_csspp_version",
            "_csspp_major",
            "_csspp_minor",
            "_csspp_patch",

            // constants.scss
            "_csspp_e",
            "_csspp_log2e",
            "_csspp_log10e",
            "_csspp_ln2e",
            "_csspp_ln10e",
            "_csspp_pi",
            "_csspp_pi_rad",
            "_csspp_sqrt2",

            // internal (generated by library)
            "_csspp_no_logo",
            "_csspp_usdate",
            "_csspp_month",
            "_csspp_day",
            "_csspp_year",
            "_csspp_time",
            "_csspp_hour",
            "_csspp_minute",
            "_csspp_second",
        };

        for(size_t idx(0); idx < sizeof(internal_variables) / sizeof(internal_variables[0]); ++idx)
        {
            char const *name = internal_variables[idx];
            std::stringstream ss;
            ss << "div { z-index: variable_exists("
               << name
               << ") ? decimal_number(\"3.14\") : 17 }\n"
               << "div { z-index: variable_exists(\""
               << name
               << "\") ? decimal_number(\"3.14\") : 17 }\n"
               << "div { z-index: variable_exists(\""
               << name
               << "_not_this_one\") ? decimal_number(\"3.14\") : 17 }\n"
               // finally, test with a sub-variable which exists
               << "div { $sub_var: 123; z-index: variable_exists(\"sub_var\") ? decimal_number(\"3.14\") : 17 }\n"
               // and then "disappeared"
               << "div { z-index: variable_exists(\"sub_var\") ? decimal_number(\"3.14\") : 17 }\n";
//std::cerr << "*** input = [" << ss.str() << "]\n";
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
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"\" D:3.14\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"\" D:3.14\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:17\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"        V:sub_var\n"
"          LIST\n"
"            VARIABLE \"sub_var\"\n"
"            INTEGER \"\" I:123\n"
"      LIST\n"
"        DECLARATION \"z-index\"\n"
"          ARG\n"
"            DECIMAL_NUMBER \"\" D:3.14\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:17\n"
+ csspp_test::get_close_comment(true)

                );

            std::stringstream assembler_out;
            csspp::assembler a(assembler_out);
            a.output(n, csspp::output_mode_t::COMPRESSED);

    //std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

            CATCH_REQUIRE(assembler_out.str() ==

"div{z-index:3.14}div{z-index:3.14}div{z-index:17}div{z-index:3.14}div{z-index:17}\n"
+ csspp_test::get_close_comment()

                    );

            CATCH_REQUIRE(c.get_root() == n);
        }
    }
    CATCH_END_SECTION()

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Expression unique_id()", "[expression] [internal-functions] [unique-id]")
{
    CATCH_START_SECTION("unique_id() without an identifier")
    {
        std::stringstream ss;
        ss << "a { content: string(unique_id()); }"
           << "b { content: string(unique_id()); }"
           << "c { content: string(unique_id()); }"
           << "d { content: string(unique_id()); }"
           << "e { content: string(unique_id()); }"
           << "f { content: string(unique_id()); }";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        // reset counter so we can compare with 1, 2, 3 each time
        csspp::expression::set_unique_id_counter(0);
        CATCH_REQUIRE(csspp::expression::get_unique_id_counter() == 0);

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

        CATCH_REQUIRE(csspp::expression::get_unique_id_counter() == 6);

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
"      IDENTIFIER \"a\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"content\"\n"
"        ARG\n"
"          STRING \"_csspp_unique1\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"b\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"content\"\n"
"        ARG\n"
"          STRING \"_csspp_unique2\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"c\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"content\"\n"
"        ARG\n"
"          STRING \"_csspp_unique3\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"d\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"content\"\n"
"        ARG\n"
"          STRING \"_csspp_unique4\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"e\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"content\"\n"
"        ARG\n"
"          STRING \"_csspp_unique5\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"f\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"content\"\n"
"        ARG\n"
"          STRING \"_csspp_unique6\"\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==

"a{content:\"_csspp_unique1\"}"
"b{content:\"_csspp_unique2\"}"
"c{content:\"_csspp_unique3\"}"
"d{content:\"_csspp_unique4\"}"
"e{content:\"_csspp_unique5\"}"
"f{content:\"_csspp_unique6\"}"
"\n"
+ csspp_test::get_close_comment()

                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("unique_id() with out own identifier")
    {
        std::stringstream ss;
        ss << "a { content: string(unique_id(my_id)); }\n"
           << "b { content: string(unique_id(\"this_id\")); }\n"
           << "c { content: string(unique_id('string')); }\n"
           << "d { content: string(unique_id(flower)); }\n"
           << "e { content: string(unique_id(id)); }\n"
           << "f { content: string(unique_id(a)); }\n";
        csspp::position pos("test.css");
        csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

        csspp::parser p(l);

        csspp::node::pointer_t n(p.stylesheet());

//std::cerr << "Parser result is: [" << *n << "]\n";

        // reset counter so we can compare with 1, 2, 3 each time
        csspp::expression::set_unique_id_counter(0);
        CATCH_REQUIRE(csspp::expression::get_unique_id_counter() == 0);

        csspp::compiler c;
        c.set_root(n);
        c.set_date_time_variables(csspp_test::get_now());
        c.add_path(csspp_test::get_script_path());
        c.add_path(csspp_test::get_version_script_path());

        c.compile(false);

        CATCH_REQUIRE(csspp::expression::get_unique_id_counter() == 6);

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
"      IDENTIFIER \"a\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"content\"\n"
"        ARG\n"
"          STRING \"my_id1\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"b\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"content\"\n"
"        ARG\n"
"          STRING \"this_id2\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"c\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"content\"\n"
"        ARG\n"
"          STRING \"string3\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"d\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"content\"\n"
"        ARG\n"
"          STRING \"flower4\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"e\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"content\"\n"
"        ARG\n"
"          STRING \"id5\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"f\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"content\"\n"
"        ARG\n"
"          STRING \"a6\"\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==

"a{content:\"my_id1\"}"
"b{content:\"this_id2\"}"
"c{content:\"string3\"}"
"d{content:\"flower4\"}"
"e{content:\"id5\"}"
"f{content:\"a6\"}"
"\n"
+ csspp_test::get_close_comment()

                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Expression if()", "[expression] [internal-functions] [if]")
{
    CATCH_START_SECTION("check that the inetrnal if() function works as expected")
    {
        std::stringstream ss;
        ss << "div { width: if(3.14 = 17, 1.22em, 44px) }\n"
           << "div { width: if(3.14 != 17, 1.23em, 45px) }\n"
           << "div { border: if(3.14 != 17, 0.2em solid black, 0.1em solid white) }\n";
//std::cerr << "*** input = [" << ss.str() << "]\n";
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
"          INTEGER \"px\" I:44\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"width\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"em\" D:1.23\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"border\"\n"
"        ARG\n"
"          LIST\n"
"            DECIMAL_NUMBER \"em\" D:0.2\n"
"            WHITESPACE\n"
"            IDENTIFIER \"solid\"\n"
"            WHITESPACE\n"
"            COLOR H:ff000000\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==

"div{width:44px}div{width:1.23em}div{border:.2em solid #000}\n"
+ csspp_test::get_close_comment()

                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Expression inspect()", "[expression] [internal-functions] [inspect]")
{
    CATCH_START_SECTION("check that the internal inspect() function works as expected")
    {
        std::stringstream ss;
        ss << "div { content: inspect(0.2em solid black) }\n";
//std::cerr << "*** input = [" << ss.str() << "]\n";
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
"      DECLARATION \"content\"\n"
"        ARG\n"
"          STRING \"0.2em solid #000\"\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==

"div{content:\"0.2em solid #000\"}\n"
+ csspp_test::get_close_comment()

                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Expression random()", "[expression] [internal-functions] [random]")
{
    CATCH_START_SECTION("check that the internal random() function \"works as expected\"")
    {
        std::stringstream ss;
        ss << "div { width: random() * 1.0px }\n";
//std::cerr << "*** input = [" << ss.str() << "]\n";
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

        // to check the compiler output we need to know what random
        // value was generated; for that purpose, we need to retrieve
        // it from the tree
        csspp::decimal_number_t v(0);
        // super ugly if() statement, note that in itself it is no
        // different than the test below which compares the compiler
        // output tree with what we expect said tree to be
        if(n->is(csspp::node_type_t::LIST)
        && n->size() > 0
        && n->get_child(0)->is(csspp::node_type_t::COMPONENT_VALUE)
        && n->get_child(0)->size() > 1
        && n->get_child(0)->get_child(1)->is(csspp::node_type_t::OPEN_CURLYBRACKET)
        && n->get_child(0)->get_child(1)->size() > 0
        && n->get_child(0)->get_child(1)->get_child(0)->is(csspp::node_type_t::DECLARATION)
        && n->get_child(0)->get_child(1)->get_child(0)->size() > 0
        && n->get_child(0)->get_child(1)->get_child(0)->get_child(0)->is(csspp::node_type_t::ARG)
        && n->get_child(0)->get_child(1)->get_child(0)->get_child(0)->size() > 0
        && n->get_child(0)->get_child(1)->get_child(0)->get_child(0)->get_child(0)->is(csspp::node_type_t::DECIMAL_NUMBER))
        {
            v = n->get_child(0)->get_child(1)->get_child(0)->get_child(0)->get_child(0)->get_decimal_number();
        }

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
"          DECIMAL_NUMBER \"px\" D:" + csspp::decimal_number_to_string(v, false) + "\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==

"div{width:" + csspp::decimal_number_to_string(v, true) + "px}\n"
+ csspp_test::get_close_comment()

                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Expression min()/max()", "[expression] [internal-functions] [min] [max]")
{
    CATCH_START_SECTION("check the min()/max() functions against a list of random numbers")
    {
        for(int i(0); i < 10; ++i)
        {
            int min(1000);
            int max(-1000);
            std::string int_out;
            std::string px_out;
            std::string flt_out;
            std::string em_out;
            std::string percent_out;
            for(int j(0); j < 10; ++j)
            {
                int n(rand() % 2001 - 1000);
                if(n < min)
                {
                    min = n;
                }
                if(n > max)
                {
                    max = n;
                }
                if(j != 0)
                {
                    int_out += ", ";
                    px_out  += ", ";
                    flt_out += ", ";
                    em_out  += ", ";
                    percent_out += ", ";
                }
                int_out += std::to_string(n);
                px_out  += std::to_string(n) + "px";
                std::stringstream flt;
                flt << std::setprecision(6) << std::fixed
                    << (static_cast<csspp::decimal_number_t>(n) / 100.0);
                flt_out += flt.str();
                em_out += flt.str() + "em";
                percent_out += std::to_string(n) + "%";
            }
            std::stringstream ss;
            ss << "div { width:  min(" << int_out << ") }\n"
               << "sub { height: max(" << int_out << ") }\n"
               << "div { width:  min(" << px_out << ") }\n"
               << "sub { height: max(" << px_out << ") }\n"
               << "div { width:  min(" << flt_out << ") }\n"
               << "sub { height: max(" << flt_out << ") }\n"
               << "div { width:  min(" << em_out << ") }\n"
               << "sub { height: max(" << em_out << ") }\n"
               << "div { width:  min(" << percent_out << ") }\n"
               << "sub { height: max(" << percent_out << ") }\n";
//std::cerr << "*** input = [" << ss.str() << "]\n";
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
"          INTEGER \"\" I:" + std::to_string(min) + "\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"sub\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"height\"\n"
"        ARG\n"
"          INTEGER \"\" I:" + std::to_string(max) + "\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"width\"\n"
"        ARG\n"
"          INTEGER \"px\" I:" + std::to_string(min) + "\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"sub\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"height\"\n"
"        ARG\n"
"          INTEGER \"px\" I:" + std::to_string(max) + "\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"width\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"\" D:" + csspp::decimal_number_to_string(min / 100.0, false) + "\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"sub\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"height\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"\" D:" + csspp::decimal_number_to_string(max / 100.0, false) + "\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"width\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"em\" D:" + csspp::decimal_number_to_string(min / 100.0, false) + "\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"sub\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"height\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"em\" D:" + csspp::decimal_number_to_string(max / 100.0, false) + "\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"width\"\n"
"        ARG\n"
"          PERCENT D:" + csspp::decimal_number_to_string(min / 100.0, false) + "\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"sub\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"height\"\n"
"        ARG\n"
"          PERCENT D:" + csspp::decimal_number_to_string(max / 100.0, false) + "\n"
+ csspp_test::get_close_comment(true)

                );

            std::stringstream assembler_out;
            csspp::assembler a(assembler_out);
            a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

            CATCH_REQUIRE(assembler_out.str() ==

"div{width:" + std::to_string(min) + "}"
"sub{height:" + std::to_string(max) + "}"
"div{width:" + std::to_string(min) + "px}"
"sub{height:" + std::to_string(max) + "px}"
"div{width:" + csspp::decimal_number_to_string(min / 100.0, true) + "}"
"sub{height:" + csspp::decimal_number_to_string(max / 100.0, true) + "}"
"div{width:" + csspp::decimal_number_to_string(min / 100.0, true) + "em}"
"sub{height:" + csspp::decimal_number_to_string(max / 100.0, true) + "em}"
"div{width:" + csspp::decimal_number_to_string(min / 1.0, true) + "%}"
"sub{height:" + csspp::decimal_number_to_string(max / 1.0, true) + "%}"
"\n"
+ csspp_test::get_close_comment()

                    );

            CATCH_REQUIRE(c.get_root() == n);
        }
    }
    CATCH_END_SECTION()

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Expression str_length()", "[expression] [internal-functions] [str-length]")
{
    CATCH_START_SECTION("check the str_length() function")
    {
        for(int i(0); i < 30; ++i)
        {
            std::stringstream ss;
            csspp::position pos("test.css");
            csspp::lexer::pointer_t l(new csspp::lexer(ss, pos));

            std::string str;
            for(int j(0); j < i; ++j)
            {
                // ensure we check UTF-8 characters (because each such
                // character must be counted as 1 even though multiple
                // bytes are used)
                csspp::wide_char_t c(0);
                do
                {
                    c = ((j & 1) == 0
                            ? rand() % 26 + 'a'
                            : rand() % (0x110000 - 0xC0) + 0xC0);
                }
                while(c < 'a' || (c >= 0xD800 && c <= 0xDFFF));
                char mb[6];
                l->wctomb(c, mb, sizeof(mb) / sizeof(mb[0]));
                str += mb;
            }
            ss << "p { z-index: str_length(\"" << str << "\") }";
//std::cerr << "*** input = [" << ss.str() << "]\n";

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
"      IDENTIFIER \"p\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:" + std::to_string(i) + "\n"
+ csspp_test::get_close_comment(true)

                );

            std::stringstream assembler_out;
            csspp::assembler a(assembler_out);
            a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

            CATCH_REQUIRE(assembler_out.str() ==

"p{z-index:" + std::to_string(i) + "}\n"
+ csspp_test::get_close_comment()

                    );

            CATCH_REQUIRE(c.get_root() == n);
        }
    }
    CATCH_END_SECTION()

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Expression type_of()", "[expression] [internal-functions] [type-of]")
{
    CATCH_START_SECTION("check the type_of() function")
    {
        std::stringstream ss;
        ss << "p { content: type_of(\"string\") }\n"
           << "div { content: type_of(12) }\n"
           << "span { content: type_of(1.12) }\n"
           << "q { content: type_of(15%) }\n"
           << "s { content: type_of('string too') }\n"
           << "b { content: type_of(true) }\n"
           << "i { content: type_of(false) }\n"
           << "sub { content: type_of(white) }\n"
           << "sup { content: type_of(#ABC) }\n"
           << "blockquote { content: type_of(U+9?\x3F) }\n"
           << "span { content: type_of(('this', 'is', an, array)) }\n"
           << "td { content: type_of((first: 'map', ever : 'tested', with :'csspp')) }\n"
           << "th { content: type_of(null) }\n"
           << "u { content: type_of(test) }\n";
//std::cerr << "*** input = [" << ss.str() << "]\n";
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
"      IDENTIFIER \"p\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"content\"\n"
"        ARG\n"
"          STRING \"string\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"content\"\n"
"        ARG\n"
"          STRING \"integer\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"span\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"content\"\n"
"        ARG\n"
"          STRING \"number\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"q\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"content\"\n"
"        ARG\n"
"          STRING \"number\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"s\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"content\"\n"
"        ARG\n"
"          STRING \"string\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"b\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"content\"\n"
"        ARG\n"
"          STRING \"bool\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"i\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"content\"\n"
"        ARG\n"
"          STRING \"bool\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"sub\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"content\"\n"
"        ARG\n"
"          STRING \"color\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"sup\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"content\"\n"
"        ARG\n"
"          STRING \"color\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"blockquote\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"content\"\n"
"        ARG\n"
"          STRING \"unicode-range\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"span\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"content\"\n"
"        ARG\n"
"          STRING \"list\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"td\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"content\"\n"
"        ARG\n"
"          STRING \"map\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"th\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"content\"\n"
"        ARG\n"
"          STRING \"undefined\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"u\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"content\"\n"
"        ARG\n"
"          STRING \"identifier\"\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==

"p{content:\"string\"}"
"div{content:\"integer\"}"
"span{content:\"number\"}"
"q{content:\"number\"}"
"s{content:\"string\"}"
"b{content:\"bool\"}"
"i{content:\"bool\"}"
"sub{content:\"color\"}"
"sup{content:\"color\"}"
"blockquote{content:\"unicode-range\"}"
"span{content:\"list\"}"
"td{content:\"map\"}"
"th{content:\"undefined\"}"
"u{content:\"identifier\"}"
"\n"
+ csspp_test::get_close_comment()

                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Expression unit()", "[expression] [internal-functions] [unit]")
{
    CATCH_START_SECTION("check the unit() function -- standard CSS units")
    {
        std::stringstream ss;
        ss << "p { content: unit(12) }\n"
           << "div { content: unit(5px) }\n"
           << "span { content: unit(1.12%) }\n"
           << "q { content: unit(15.43em) }\n";
//std::cerr << "*** input = [" << ss.str() << "]\n";
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
"      IDENTIFIER \"p\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"content\"\n"
"        ARG\n"
"          STRING \"\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"content\"\n"
"        ARG\n"
"          STRING \"px\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"span\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"content\"\n"
"        ARG\n"
"          STRING \"%\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"q\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"content\"\n"
"        ARG\n"
"          STRING \"em\"\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==

"p{content:\"\"}"
"div{content:\"px\"}"
"span{content:\"%\"}"
"q{content:\"em\"}"
"\n"
+ csspp_test::get_close_comment()

                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("check the unit() function -- non-standard CSS units")
    {
        std::stringstream ss;
        ss << "p { content: unit(12px ** 2 / 17em) }\n"
           << "div { content: unit(5px ** 3) }\n"
           << "span { content: unit(1.12cm ** 3 / (3em ** 2)) }\n";
//std::cerr << "*** input = [" << ss.str() << "]\n";
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
"      IDENTIFIER \"p\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"content\"\n"
"        ARG\n"
"          STRING \"px * px / em\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"div\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"content\"\n"
"        ARG\n"
"          STRING \"px * px * px\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"span\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"content\"\n"
"        ARG\n"
"          STRING \"cm * cm * cm / em * em\"\n"
+ csspp_test::get_close_comment(true)

            );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Expression decimal_number()/integer()/percentage()/string()/identifier()", "[expression] [internal-functions] [decimal-number] [integer] [percentage] [string] [identifier]")
{
    CATCH_START_SECTION("check conversions to decimal number")
    {
        std::stringstream ss;
        ss << "div { z-index: decimal_number(314) }\n"
           << "span { z-index: decimal_number(\"3.14\") }\n"
           << "p { z-index: decimal_number('3.14px') }\n"
           << "i { z-index: decimal_number(\\33\\.14) }\n"
           << "q { z-index: decimal_number(3.14%) }\n"
           << "s { z-index: decimal_number(\" 123 \") }\n"
           << "b { z-index: decimal_number(\"123\") }\n"
           << "u { z-index: decimal_number(1.23) }\n"
           << "blockquote { z-index: decimal_number(\"1.23%\") }\n";
//std::cerr << "*** from " << ss.str() << "\n";
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
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"\" D:314\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"span\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"\" D:3.14\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"p\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"px\" D:3.14\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"i\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"\" D:3.14\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"q\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"\" D:0.031\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"s\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"\" D:123\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"b\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"\" D:123\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"u\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"\" D:1.23\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"blockquote\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          DECIMAL_NUMBER \"\" D:0.012\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==

"div{z-index:314}"
"span{z-index:3.14}"
"p{z-index:3.14px}"
"i{z-index:3.14}"
"q{z-index:.031}"
"s{z-index:123}"
"b{z-index:123}"
"u{z-index:1.23}"
"blockquote{z-index:.012}"
"\n"
+ csspp_test::get_close_comment()

                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("check conversions to integer")
    {
        std::stringstream ss;
        ss << "div { z-index: integer(314) }\n"
           << "span { z-index: integer(\"3.14\") }\n"
           << "p { z-index: integer('3.14px') }\n"
           << "i { z-index: integer(\\33\\.14) }\n"
           << "q { z-index: integer(314%) }\n"
           << "s { z-index: integer(\" 123 \") }\n"
           << "b { z-index: integer(\"123\") }\n"
           << "u { z-index: integer(1.23) }\n"
           << "blockquote { z-index: integer('314%') }\n";
//std::cerr << "*** from " << ss.str() << "\n";
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
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:314\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"span\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:3\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"p\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"px\" I:3\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"i\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:3\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"q\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:3\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"s\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:123\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"b\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:123\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"u\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          INTEGER \"\" I:1\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"blockquote\"\n"
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

"div{z-index:314}"
"span{z-index:3}"
"p{z-index:3px}"
"i{z-index:3}"
"q{z-index:3}"
"s{z-index:123}"
"b{z-index:123}"
"u{z-index:1}"
"blockquote{z-index:3}"
"\n"
+ csspp_test::get_close_comment()

                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("check conversions to percentage")
    {
        std::stringstream ss;
        ss << "div { z-index: percentage(314) }\n"
           << "span { z-index: percentage(\"3.14\") }\n"
           << "p { z-index: percentage('3.14px') }\n"
           << "i { z-index: percentage(\\33\\.14) }\n"
           << "q { z-index: percentage(3.14%) }\n"
           << "s { z-index: percentage(\" 123 \") }\n"
           << "b { z-index: percentage(\"123\") }\n"
           << "u { z-index: percentage(1.23) }\n"
           << "blockquote { z-index: percentage(\"1.23%\") }\n";
//std::cerr << "*** from " << ss.str() << "\n";
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
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          PERCENT D:314\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"span\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          PERCENT D:3.14\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"p\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          PERCENT D:3.14\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"i\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          PERCENT D:3.14\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"q\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          PERCENT D:0.031\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"s\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          PERCENT D:123\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"b\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          PERCENT D:123\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"u\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          PERCENT D:1.23\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"blockquote\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          PERCENT D:0.012\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==

"div{z-index:31400%}"
"span{z-index:314%}"
"p{z-index:314%}"
"i{z-index:314%}"
"q{z-index:3.14%}"
"s{z-index:12300%}"
"b{z-index:12300%}"
"u{z-index:123%}"
"blockquote{z-index:1.23%}"
"\n"
+ csspp_test::get_close_comment()

                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("check conversions to string")
    {
        std::stringstream ss;
        ss << "div { z-index: string(314) }\n"
           << "span { z-index: string(\"3.14\") }\n"
           << "p { z-index: string('3.14px') }\n"
           << "i { z-index: string(\\33\\.14) }\n"
           << "q { z-index: string(3.14%) }\n"
           << "s { z-index: string(\" 123 \") }\n"
           << "b { z-index: string(\"123\") }\n"
           << "u { z-index: string(1.23) }\n";
//std::cerr << "*** from " << ss.str() << "\n";
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
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          STRING \"314\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"span\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          STRING \"3.14\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"p\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          STRING \"3.14px\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"i\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          STRING \"3.14\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"q\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          STRING \"3.14%\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"s\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          STRING \" 123 \"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"b\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          STRING \"123\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"u\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          STRING \"1.23\"\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==

"div{z-index:\"314\"}"
"span{z-index:\"3.14\"}"
"p{z-index:\"3.14px\"}"
"i{z-index:\"3.14\"}"
"q{z-index:\"3.14%\"}"
"s{z-index:\" 123 \"}"
"b{z-index:\"123\"}"
"u{z-index:\"1.23\"}"
"\n"
+ csspp_test::get_close_comment()

                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("check conversions to identifiers")
    {
        std::stringstream ss;
        ss << "div { z-index: identifier(test) }\n"
           << "span { z-index: identifier(\"test\") }\n"
           << "p { z-index: identifier('test') }\n"
           << "i { z-index: identifier(123) }\n"
           << "q { z-index: identifier(1.23%) }\n"
           << "s { z-index: identifier(\" 123 \") }\n"
           << "b { z-index: identifier(\"123\") }\n"
           << "u { z-index: identifier(1.23) }\n";
//std::cerr << "*** from " << ss.str() << "\n";
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
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          IDENTIFIER \"test\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"span\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          IDENTIFIER \"test\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"p\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          IDENTIFIER \"test\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"i\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          IDENTIFIER \"123\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"q\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          IDENTIFIER \"1.23%\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"s\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          IDENTIFIER \" 123 \"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"b\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          IDENTIFIER \"123\"\n"
"  COMPONENT_VALUE\n"
"    ARG\n"
"      IDENTIFIER \"u\"\n"
"    OPEN_CURLYBRACKET B:true\n"
"      DECLARATION \"z-index\"\n"
"        ARG\n"
"          IDENTIFIER \"1.23\"\n"
+ csspp_test::get_close_comment(true)

            );

        std::stringstream assembler_out;
        csspp::assembler a(assembler_out);
        a.output(n, csspp::output_mode_t::COMPRESSED);

//std::cerr << "----------------- Result is " << static_cast<csspp::output_mode_t>(i) << "\n[" << out.str() << "]\n";

        CATCH_REQUIRE(assembler_out.str() ==

"div{z-index:test}"
"span{z-index:test}"
"p{z-index:test}"
"i{z-index:\\31 23}"
"q{z-index:\\31\\.23\\%}"
"s{z-index:\\ 123\\ }"
"b{z-index:\\31 23}"
"u{z-index:\\31\\.23}"
"\n"
+ csspp_test::get_close_comment()

                );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Invalid sub-expression decimal_number()/integer()/string()/identifier()", "[expression] [internal-functions] [decimal-number] [integer] [string] [identifier] [invalid]")
{
    CATCH_START_SECTION("check conversions to decimal number with an invalid string")
    {
        std::stringstream ss;
        ss << "div { z-index: decimal_number(\"invalid\") }\n";
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

        VERIFY_ERRORS("test.css(1): error: decimal_number() expects a string parameter to represent a valid integer, decimal number, or percent value.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("check decimal number without a parameter")
    {
        std::stringstream ss;
        ss << "div { z-index: decimal_number() }\n";
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

        VERIFY_ERRORS("test.css(1): error: decimal_number() expects exactly 1 parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("check conversions to decimal number with a unicode range")
    {
        std::stringstream ss;
        ss << "div { z-index: decimal_number(U+1-5) }\n";
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

        VERIFY_ERRORS(
                "test.css(1): error: decimal_number() expects one value as parameter.\n"
            );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("check conversions to integer with an invalid string")
    {
        std::stringstream ss;
        ss << "div { z-index: integer(\"invalid\") }\n";
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

        VERIFY_ERRORS("test.css(1): error: decimal_number() expects a string parameter to represent a valid integer, decimal number, or percent value.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("check conversions to integer with an invalid expression as parameter")
    {
        std::stringstream ss;
        ss << "div { z-index: integer(?) }\n";
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

        VERIFY_ERRORS(
                "test.css(1): error: unsupported type CONDITIONAL as a unary expression token.\n"
                "test.css(1): error: integer() expects one value as parameter.\n"
            );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("check conversions to integer with a unicode range")
    {
        std::stringstream ss;
        ss << "div { z-index: integer(U+1-5) }\n";
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

        VERIFY_ERRORS(
                "test.css(1): error: integer() expects one value as parameter.\n"
            );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    // no error left over
    VERIFY_ERRORS("");
}

CATCH_TEST_CASE("Expression calling functions with invalid parameters", "[expression] [internal-functions] [invalid]")
{
    CATCH_START_SECTION("abs(\"wrong\")")
    {
        std::stringstream ss;
        ss << "div { width: abs(\"wrong\"); }";
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

        VERIFY_ERRORS("test.css(1): error: abs() expects a number as parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("acos(true)")
    {
        std::stringstream ss;
        ss << "div { width: acos(true); }";
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

        VERIFY_ERRORS("test.css(1): error: acos() expects a number as parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("alpha(12)")
    {
        std::stringstream ss;
        ss << "div { width: alpha(12); }";
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

        VERIFY_ERRORS("test.css(1): error: alpha() expects a color as parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("asin(U+4\x3F?)")
    {
        std::stringstream ss;
        ss << "div { width: asin(U+4\x3F?); }";
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

        VERIFY_ERRORS("test.css(1): error: asin() expects a number as parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("atan(U+1-2)")
    {
        std::stringstream ss;
        ss << "div { width: atan(U+1-2); }";
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

        VERIFY_ERRORS("test.css(1): error: atan() expects a number as parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("blue(15)")
    {
        std::stringstream ss;
        ss << "div { width: blue(15); }";
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

        VERIFY_ERRORS("test.css(1): error: blue() expects a color as parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("ceil(false)")
    {
        std::stringstream ss;
        ss << "div { width: ceil(false); }";
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

        VERIFY_ERRORS("test.css(1): error: ceil() expects a number as parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("cos(white)")
    {
        std::stringstream ss;
        ss << "div { width: cos(white); }";
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

        VERIFY_ERRORS("test.css(1): error: cos() expects an angle as parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("floor(false)")
    {
        std::stringstream ss;
        ss << "div { width: floor(false); }";
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

        VERIFY_ERRORS("test.css(1): error: floor() expects a number as parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("frgb(\"200\")")
    {
        std::stringstream ss;
        ss << "div { width: frgb(\"200\"); }";
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

        VERIFY_ERRORS("test.css(1): error: frgb() expects exactly one color parameter or three numbers (Red, Green, Blue).\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("frgb(1, 2, 3, 4, 5)")
    {
        std::stringstream ss;
        ss << "div { width: frgb(1, 2, 3, 4, 5); }";
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

        VERIFY_ERRORS("test.css(1): error: frgb() expects between 1 and 3 parameters.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("frgba(\"200\", 1.0)")
    {
        std::stringstream ss;
        ss << "div { width: frgba(\"200\", 1.0); }";
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

        VERIFY_ERRORS("test.css(1): error: frgba() expects exactly one color parameter followed by one number (Color, Alpha), or four numbers (Red, Green, Blue, Alpha).\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("function_exists(200)")
    {
        std::stringstream ss;
        ss << "div { width: function_exists(200); }";
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

        VERIFY_ERRORS("test.css(1): error: function_exists() expects a string or an identifier as parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("global_variable_exists(200)")
    {
        std::stringstream ss;
        ss << "div { width: global_variable_exists(200); }";
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

        VERIFY_ERRORS("test.css(1): error: global_variable_exists() expects a string or an identifier as parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("green(1 = 5)")
    {
        std::stringstream ss;
        ss << "div { width: green(1 = 5); }";
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

        VERIFY_ERRORS("test.css(1): error: green() expects a color as parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("hsl(5, '3', 2)")
    {
        std::stringstream ss;
        ss << "div { width: hsl(5, '3', 2); }";
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

        VERIFY_ERRORS("test.css(1): error: hsl() expects exactly three numbers: Hue (angle), Saturation (%), and Lightness (%).\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("hsl(3deg, 3%)") // 3rd % is missing
    {
        std::stringstream ss;
        ss << "div { width: hsl(U+3?\?); }";
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

        VERIFY_ERRORS("test.css(1): error: hsl() expects exactly 3 parameters.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("hsla(5, '3', 2, 0.4)")
    {
        std::stringstream ss;
        ss << "div { width: hsla(5, '3', 2, 0.4); }";
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

        VERIFY_ERRORS("test.css(1): error: hsla() expects exactly four numbers: Hue (angle), Saturation (%), Lightness (%), and Alpha (0.0 to 1.0).\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("hue('string')")
    {
        std::stringstream ss;
        ss << "div { width: hue('string'); }";
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

        VERIFY_ERRORS("test.css(1): error: hue() expects a color as parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("identifier(U+333)")
    {
        std::stringstream ss;
        ss << "div { width: identifier(U+333); }";
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

        VERIFY_ERRORS("test.css(1): error: identifier() expects one value as parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("if(true false)")
    {
        std::stringstream ss;
        ss << "div { width: if(true false, result if true, result if false); }";
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

        VERIFY_ERRORS("test.css(1): error: if() expects a boolean as its first argument.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("lightness(3px solid #439812)")
    {
        std::stringstream ss;
        ss << "div { width: lightness(3px solid #439812); }";
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

        VERIFY_ERRORS("test.css(1): error: lightness() expects a color as parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("log(5.3px)")
    {
        std::stringstream ss;
        ss << "div { width: log(5.3px); }";
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

        VERIFY_ERRORS("test.css(1): error: log() expects a unit less number as parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("log(0.0)")
    {
        std::stringstream ss;
        ss << "div { width: log(0.0); }";
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

        VERIFY_ERRORS("test.css(1): error: log() expects a positive number as parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("log(-7)")
    {
        std::stringstream ss;
        ss << "div { width: log(-7); }";
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

        VERIFY_ERRORS("test.css(1): error: log() expects a positive number as parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("log(\"not accepted\")")
    {
        std::stringstream ss;
        ss << "div { width: log(\"not accepted\"); }";
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

        VERIFY_ERRORS("test.css(1): error: log() expects a number as parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("max(3px, 5em)")
    {
        std::stringstream ss;
        ss << "div { width: max(3px, 5em); }";
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

        VERIFY_ERRORS("test.css(1): error: max() expects all numbers to have the same dimension.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("max(3px 5px, 1px 3px)")
    {
        std::stringstream ss;
        ss << "div { width: max(3px, 5em); }";
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

        VERIFY_ERRORS("test.css(1): error: max() expects all numbers to have the same dimension.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("max('strings', 'are', 'illegal', 'here')")
    {
        std::stringstream ss;
        ss << "div { width: max('strings', 'are', 'illegal', 'here'); }";
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

        VERIFY_ERRORS(
                "test.css(1): error: max() expects any number of numbers.\n"
                "test.css(1): error: max() expects any number of numbers.\n"
                "test.css(1): error: max() expects any number of numbers.\n"
                "test.css(1): error: max() expects any number of numbers.\n"
            );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("min(3px, 5em)")
    {
        std::stringstream ss;
        ss << "div { width: min(3px, 5em); }";
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

        VERIFY_ERRORS("test.css(1): error: min() expects all numbers to have the same dimension.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("min(3px 5px, 1px 3px)")
    {
        std::stringstream ss;
        ss << "div { width: min(3px, 5em); }";
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

        VERIFY_ERRORS("test.css(1): error: min() expects all numbers to have the same dimension.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("min('strings', 'are', 'illegal', 'here')")
    {
        std::stringstream ss;
        ss << "div { width: min('strings', 'are', 'illegal', 'here'); }";
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

        VERIFY_ERRORS(
                "test.css(1): error: min() expects any number of numbers.\n"
                "test.css(1): error: min() expects any number of numbers.\n"
                "test.css(1): error: min() expects any number of numbers.\n"
                "test.css(1): error: min() expects any number of numbers.\n"
            );

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("not(U+78-7F)")
    {
        std::stringstream ss;
        ss << "div { width: not(U+78-7F); }";
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

        VERIFY_ERRORS("test.css(1): error: a boolean expression was expected.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("not(true false)")
    {
        std::stringstream ss;
        ss << "div { width: not(true false); }";
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

        VERIFY_ERRORS("test.css(1): error: not() expects a boolean as its first argument.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("percentage(U+333)")
    {
        std::stringstream ss;
        ss << "div { width: percentage(U+333); }";
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

        VERIFY_ERRORS("test.css(1): error: percentage() expects one value as parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("percentage(\"not a number\")")
    {
        std::stringstream ss;
        ss << "div { width: percentage(\"not a number\"); }";
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

        VERIFY_ERRORS("test.css(1): error: percentage() expects a string parameter to represent a valid integer, decimal number, or percent value.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("red(15)")
    {
        std::stringstream ss;
        ss << "div { width: red(15); }";
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

        VERIFY_ERRORS("test.css(1): error: red() expects a color as parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("rgb(\"200\")")
    {
        std::stringstream ss;
        ss << "div { width: rgb(\"200\"); }";
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

        VERIFY_ERRORS("test.css(1): error: rgb() expects exactly one color parameter (Color) or three numbers (Red, Green, Blue).\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("rgb(red green blue)")
    {
        std::stringstream ss;
        ss << "div { width: rgb(red green blue); }";
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

        VERIFY_ERRORS("test.css(1): error: rgb() expects exactly one color parameter (Color) or three numbers (Red, Green, Blue).\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("rgba(\"200\", 1.0)")
    {
        std::stringstream ss;
        ss << "div { width: rgba(\"200\", 1.0); }";
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

        VERIFY_ERRORS("test.css(1): error: rgba() expects exactly one color parameter followed by alpha (Color, Alpha) or four numbers (Red, Green, Blue, Alpha).\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("round(false)")
    {
        std::stringstream ss;
        ss << "div { width: round(false); }";
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

        VERIFY_ERRORS("test.css(1): error: round() expects a number as parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("saturation(U+3?\?)")
    {
        std::stringstream ss;
        ss << "div { width: saturation(U+3?\?); }";
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

        VERIFY_ERRORS("test.css(1): error: saturation() expects a color as parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("sign('number')")
    {
        std::stringstream ss;
        ss << "div { width: sign('number'); }";
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

        VERIFY_ERRORS("test.css(1): error: sign() expects a number as parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("sin('number')")
    {
        std::stringstream ss;
        ss << "div { width: sin('number'); }";
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

        VERIFY_ERRORS("test.css(1): error: sin() expects an angle as parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("sqrt(-4.0)")
    {
        std::stringstream ss;
        ss << "div { width: sqrt(-4.0); }";
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

        VERIFY_ERRORS("test.css(1): error: sqrt() expects zero or a positive number.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("sqrt(4.0px)")
    {
        std::stringstream ss;
        ss << "div { width: sqrt(4.0px); }";
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

        VERIFY_ERRORS("test.css(1): error: sqrt() expects dimensions to be squarely defined (i.e. 'px * px').\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("sqrt(4.0px*px/em*cm)")
    {
        std::stringstream ss;
        ss << "div { width: sqrt(4.0px\\*px\\/em\\*cm); }";
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

        VERIFY_ERRORS("test.css(1): error: sqrt() expects dimensions to be squarely defined (i.e. 'px * px').\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("sqrt(4.0px*cm/em*em)")
    {
        std::stringstream ss;
        ss << "div { width: sqrt(4.0px\\*cm\\/em\\*em); }";
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

        VERIFY_ERRORS("test.css(1): error: sqrt() expects dimensions to be squarely defined (i.e. 'px * px').\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("sqrt(35%)")
    {
        std::stringstream ss;
        ss << "div { width: sqrt(35%); }";
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

        VERIFY_ERRORS("test.css(1): error: sqrt() expects a number as parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("sqrt('number')")
    {
        std::stringstream ss;
        ss << "div { width: sqrt('number'); }";
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

        VERIFY_ERRORS("test.css(1): error: sqrt() expects a number as parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("sqrt(12px dashed chocolate)")
    {
        std::stringstream ss;
        ss << "div { width: sqrt(12px dashed chocolate); }";
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

        VERIFY_ERRORS("test.css(1): error: sqrt() expects a number as parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("string(U+110-11f)")
    {
        std::stringstream ss;
        ss << "div { width: string(U+110-11f); }";
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

        VERIFY_ERRORS("test.css(1): error: string() expects one value as parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("str_length(110)")
    {
        std::stringstream ss;
        ss << "div { width: str_length(110); }";
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

        VERIFY_ERRORS("test.css(1): error: str_length() expects one string as parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("str_length(10px solid blue)")
    {
        std::stringstream ss;
        ss << "div { width: str_length(10px solid blue); }";
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

        VERIFY_ERRORS("test.css(1): error: str_length() expects one string as parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("tan(true)")
    {
        std::stringstream ss;
        ss << "div { width: tan(true); }";
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

        VERIFY_ERRORS("test.css(1): error: tan() expects an angle as parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("tan(30px)")
    {
        std::stringstream ss;
        ss << "div { width: tan(30px); }";
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

        VERIFY_ERRORS("test.css(1): error: trigonometry functions expect an angle (deg, grad, rad, turn) as a parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("type_of(30pear 3carrot 15apple)")
    {
        std::stringstream ss;
        ss << "div { width: type_of(30pear 3carrot 15apple); }";
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

        VERIFY_ERRORS("test.css(1): error: type_of() expects one value as a parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("unique_id() with an integer")
    {
        std::stringstream ss;
        ss << "a { z-index: unique_id(33); }\n";
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

        VERIFY_ERRORS("test.css(1): error: unique_id() expects a string or an identifier as its optional parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("unique_id() with a unicode range")
    {
        std::stringstream ss;
        ss << "a { z-index: unique_id(U+33-44); }\n";
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

        VERIFY_ERRORS("test.css(1): error: unique_id() expects a string or an identifier as its optional parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("unit('string')")
    {
        std::stringstream ss;
        ss << "div { width: unit('string'); }";
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

        VERIFY_ERRORS("test.css(1): error: unit() expects a number as parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("unit(5px dashed tan)")
    {
        std::stringstream ss;
        ss << "div { width: unit(5px dashed tan); }";
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

        VERIFY_ERRORS("test.css(1): error: unit() expects a number as parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("variable_exists(200)")
    {
        std::stringstream ss;
        ss << "div { width: variable_exists(200); }";
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

        VERIFY_ERRORS("test.css(1): error: variable_exists() expects a string or an identifier as parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    CATCH_START_SECTION("variable_exists(3px solid white)")
    {
        std::stringstream ss;
        ss << "div { width: variable_exists(3px solid white); }";
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

        VERIFY_ERRORS("test.css(1): error: variable_exists() expects a string or an identifier as parameter.\n");

        CATCH_REQUIRE(c.get_root() == n);
    }
    CATCH_END_SECTION()

    // no error left over
    VERIFY_ERRORS("");
}

// vim: ts=4 sw=4 et
